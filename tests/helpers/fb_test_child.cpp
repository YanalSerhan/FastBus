// fb_test_child: the program cross-process tests start (M2.2). One role per invocation:
//
//   fb_test_child [--watchdog-ms N] <role> [args...]
//
//   exit <code>                       exit with <code>
//   sleep <ms>                        sleep, then exit 0
//   hang                              sleep forever (the watchdog ends it with 124)
//   shm-write <name> <offset> <byte>  open segment <name>, write <byte> at <offset>, exit 0
//   shm-check <name> <offset> <byte>  exit 0 if the byte at <offset> equals <byte>, else 3
//   shm-identity <name> <sleep-ms>    write this process's ProcessIdentity at offset 0, sleep
//   shm-args <name> [args...]         write the received args (u32 count, then u32 length +
//                                     bytes each) at offset 0, so the parent can check quoting
//
// Exit codes: 0 ok, 2 usage, 3 check failed, 4 shared-memory error, 124 watchdog fired.
// The watchdog (default 30 s) guarantees that a forgotten or hung child never hangs CI.
#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string_view>
#include <thread>

#include "fastbus/platform/process.hpp"
#include "fastbus/platform/shared_memory.hpp"
#include "fastbus/platform/time_source.hpp"

namespace {

namespace platform = fastbus::platform;

constexpr int kUsage = 2;
constexpr int kCheckFailed = 3;
constexpr int kShmError = 4;
constexpr int kWatchdog = 124;
constexpr std::int64_t kDefaultWatchdogMs = 30'000;
constexpr std::int64_t kNsPerMs = 1'000'000;

bool parse(std::string_view text, std::int64_t& out) {
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
    return ec == std::errc{} && end == text.data() + text.size();
}

void sleep_ms(std::int64_t ms) {
    platform::sleep_for_ns(ms * kNsPerMs);
}

int shm_write(std::span<const std::string_view> a, bool check) {
    std::int64_t offset = 0;
    std::int64_t value = 0;
    if (a.size() != 3 || !parse(a[1], offset) || !parse(a[2], value) || offset < 0) {
        return kUsage;
    }
    auto shm = platform::SharedMemory::open(a[0]);
    if (!shm || static_cast<std::size_t>(offset) >= shm->size()) {
        return kShmError;
    }
    std::byte& target = shm->bytes()[static_cast<std::size_t>(offset)];
    if (check) {
        return target == static_cast<std::byte>(value) ? 0 : kCheckFailed;
    }
    target = static_cast<std::byte>(value);
    return 0;
}

int shm_identity(std::span<const std::string_view> a) {
    std::int64_t ms = 0;
    if (a.size() != 2 || !parse(a[1], ms)) {
        return kUsage;
    }
    auto shm = platform::SharedMemory::open(a[0]);
    if (!shm) {
        return kShmError;
    }
    const platform::ProcessIdentity me = platform::current_process();
    std::memcpy(shm->data(), &me, sizeof(me));
    sleep_ms(ms);
    return 0;
}

int shm_args(std::span<const std::string_view> a) {
    if (a.empty()) {
        return kUsage;
    }
    auto shm = platform::SharedMemory::open(a[0]);
    if (!shm) {
        return kShmError;
    }
    const std::span<std::byte> out = shm->bytes();
    std::size_t pos = 0;
    const auto put = [&](const void* src, std::size_t n) {
        if (pos + n > out.size()) {
            return false;
        }
        std::memcpy(out.subspan(pos).data(), src, n);
        pos += n;
        return true;
    };
    const auto count = static_cast<std::uint32_t>(a.size() - 1);
    bool ok = put(&count, sizeof(count));
    for (const std::string_view arg : a.subspan(1)) {
        const auto length = static_cast<std::uint32_t>(arg.size());
        ok = ok && put(&length, sizeof(length)) && put(arg.data(), arg.size());
    }
    return ok ? 0 : kShmError;
}

int run(std::span<const std::string_view> args) {
    if (args.empty()) {
        return kUsage;
    }
    const std::string_view role = args[0];
    const auto rest = args.subspan(1);
    std::int64_t n = 0;
    if (role == "exit" && rest.size() == 1 && parse(rest[0], n)) {
        return static_cast<int>(n);
    }
    if (role == "sleep" && rest.size() == 1 && parse(rest[0], n)) {
        sleep_ms(n);
        return 0;
    }
    if (role == "hang") {
        while (true) {
            sleep_ms(1'000);
        }
    }
    if (role == "shm-write") {
        return shm_write(rest, false);
    }
    if (role == "shm-check") {
        return shm_write(rest, true);
    }
    if (role == "shm-identity") {
        return shm_identity(rest);
    }
    if (role == "shm-args") {
        return shm_args(rest);
    }
    return kUsage;
}

}  // namespace

int main(int argc, char** argv) {
    // Copy argv into string_views once; nothing below touches raw argv.
    constexpr std::size_t kMaxArgs = 64;
    std::array<std::string_view, kMaxArgs> storage{};
    const std::size_t count = std::min(static_cast<std::size_t>(argc), kMaxArgs);
    const std::span<char*> raw{argv, count};
    for (std::size_t i = 0; i < count; ++i) {
        storage.at(i) = raw[i];
    }
    std::span<const std::string_view> args{storage.data() + 1, count - 1};

    std::int64_t watchdog_ms = kDefaultWatchdogMs;
    if (args.size() >= 2 && args[0] == "--watchdog-ms") {
        if (!parse(args[1], watchdog_ms)) {
            return kUsage;
        }
        args = args.subspan(2);
    }
    std::thread([watchdog_ms] {
        sleep_ms(watchdog_ms);
        std::_Exit(kWatchdog);
    }).detach();

    return run(args);
}
