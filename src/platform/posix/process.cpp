// POSIX (Linux) backend of fastbus/platform/process.hpp. posix_spawn instead of fork: forking a
// multi-threaded process (gtest + test threads) is unsafe, posix_spawn is not.
#include "fastbus/platform/process.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <fcntl.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "fastbus/core/error.hpp"
#include "fastbus/core/result.hpp"
#include "fastbus/platform/time_source.hpp"

// POSIX: the process environment, passed on to children. Declared by no standard header.
// NOLINTNEXTLINE(readability-redundant-declaration,cppcoreguidelines-avoid-non-const-global-variables)
extern char** environ;

namespace fastbus::platform {
namespace {

constexpr std::int64_t kPollIntervalNs = 1'000'000;  // 1 ms between waitpid(WNOHANG) polls
constexpr int kSignalExitBase = 128;                 // shell convention: 128 + signal number

struct ProcStat {
    char state;               // 'R', 'S', 'Z' (zombie), 'X' (dead), ...
    std::uint64_t starttime;  // field 22: clock ticks after boot
};

/// Reads state and start time from /proc/<pid>/stat. Returns false if the process is unknown.
bool read_proc_stat(std::uint64_t pid, ProcStat& out) noexcept {
    std::array<char, 64> path{};
    constexpr std::string_view kHead = "/proc/";
    constexpr std::string_view kTail = "/stat";
    char* p = std::copy(kHead.begin(), kHead.end(), path.begin());
    p = std::to_chars(p, path.data() + path.size() - kTail.size() - 1, pid).ptr;
    p = std::copy(kTail.begin(), kTail.end(), p);
    *p = '\0';

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): open(2) is declared variadic
    const int fd = ::open(path.data(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    std::array<char, 1024> buffer{};
    const ssize_t n = ::read(fd, buffer.data(), buffer.size() - 1);
    static_cast<void>(::close(fd));
    if (n <= 0) {
        return false;
    }
    // Field 2 (comm) is "(name)" and may itself contain spaces or ')': start after the LAST ')'.
    const std::string_view text{buffer.data(), static_cast<std::size_t>(n)};
    const std::size_t close_paren = text.rfind(')');
    if (close_paren == std::string_view::npos || close_paren + 2 >= text.size()) {
        return false;
    }
    std::string_view rest = text.substr(close_paren + 2);  // starts at field 3 (state)
    out.state = rest.front();
    // Skip fields 3..21 (19 fields) to reach field 22.
    for (int field = 3; field < 22; ++field) {
        const std::size_t space = rest.find(' ');
        if (space == std::string_view::npos) {
            return false;
        }
        rest.remove_prefix(space + 1);
    }
    const auto [end, ec] = std::from_chars(rest.data(), rest.data() + rest.size(), out.starttime);
    return ec == std::errc{};
}

core::Unexpected<core::Error> spawn_error(int err) noexcept {
    switch (err) {
        case ENOENT:
        case ENOTDIR:
            return core::fail(core::ErrorCode::NotFound, "posix_spawn: executable not found");
        case EACCES:
        case EPERM:
            return core::fail(core::ErrorCode::PermissionDenied, "posix_spawn: permission denied");
        default:
            return core::fail(core::ErrorCode::SystemError, "posix_spawn failed");
    }
}

int decode_status(int status) noexcept {
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return kSignalExitBase + WTERMSIG(status);
    }
    return -1;  // stopped/continued are not reported by our waitpid flags
}

}  // namespace

ProcessIdentity current_process() noexcept {
    const auto pid = static_cast<std::uint64_t>(::getpid());
    ProcStat stat{};
    static_cast<void>(read_proc_stat(pid, stat));  // /proc/self always exists on Linux
    return ProcessIdentity{.pid = pid, .start_time = stat.starttime};
}

core::Result<ProcessIdentity> identity_of(std::uint64_t pid) noexcept {
    ProcStat stat{};
    if (pid == 0 || !read_proc_stat(pid, stat) || stat.state == 'Z' || stat.state == 'X') {
        return core::fail(core::ErrorCode::NotFound, "no running process with this pid");
    }
    return ProcessIdentity{.pid = pid, .start_time = stat.starttime};
}

bool is_alive(const ProcessIdentity& who) noexcept {
    const auto now = identity_of(who.pid);
    return now.has_value() && now->start_time == who.start_time;
}

core::Result<Process> Process::spawn(std::string_view path,
                                     std::span<const std::string_view> args) noexcept {
    if (path.empty()) {
        return core::fail(core::ErrorCode::InvalidArgument, "empty executable path");
    }
    // Setup path: building argv allocates, which is fine outside the hot path.
    std::vector<std::string> storage;
    storage.reserve(args.size() + 1);
    storage.emplace_back(path);
    for (const std::string_view arg : args) {
        storage.emplace_back(arg);
    }
    std::vector<char*> argv;
    argv.reserve(storage.size() + 1);
    for (std::string& s : storage) {
        argv.push_back(s.data());
    }
    argv.push_back(nullptr);

    pid_t child = 0;
    const int err =
        posix_spawn(&child, storage.front().c_str(), nullptr, nullptr, argv.data(), environ);
    if (err != 0) {
        return spawn_error(err);
    }
    Process process;
    process.pid_ = static_cast<std::uint64_t>(child);
    return process;
}

core::Result<int> Process::wait(std::chrono::milliseconds timeout) noexcept {
    if (exited_) {
        return exit_code_;
    }
    const std::int64_t deadline =
        monotonic_now_ns() + std::chrono::duration_cast<std::chrono::nanoseconds>(timeout).count();
    while (true) {
        int status = 0;
        const pid_t r = ::waitpid(static_cast<pid_t>(pid_), &status, WNOHANG);
        if (r > 0) {
            exited_ = true;
            exit_code_ = decode_status(status);
            return exit_code_;
        }
        if (r < 0 && errno != EINTR) {
            return core::fail(core::ErrorCode::SystemError, "waitpid failed");
        }
        if (monotonic_now_ns() >= deadline) {
            return core::fail(core::ErrorCode::Timeout, "child process still running");
        }
        sleep_for_ns(kPollIntervalNs);
    }
}

core::Result<void> Process::kill() noexcept {
    if (exited_) {
        return {};
    }
    if (::kill(static_cast<pid_t>(pid_), SIGKILL) != 0 && errno != ESRCH) {
        return core::fail(core::ErrorCode::SystemError, "kill(SIGKILL) failed");
    }
    int status = 0;
    pid_t r = 0;
    do {
        r = ::waitpid(static_cast<pid_t>(pid_), &status, 0);
    } while (r < 0 && errno == EINTR);
    if (r < 0) {
        return core::fail(core::ErrorCode::SystemError, "waitpid after kill failed");
    }
    exited_ = true;
    exit_code_ = decode_status(status);
    return {};
}

bool Process::running() noexcept {
    if (exited_ || pid_ == 0) {
        return false;
    }
    int status = 0;
    const pid_t r = ::waitpid(static_cast<pid_t>(pid_), &status, WNOHANG);
    if (r > 0) {
        exited_ = true;
        exit_code_ = decode_status(status);
        return false;
    }
    return r == 0;
}

void Process::reset() noexcept {
    if (pid_ != 0 && !exited_) {
        static_cast<void>(kill());
    }
    pid_ = 0;
    exited_ = false;
    exit_code_ = 0;
}

}  // namespace fastbus::platform
