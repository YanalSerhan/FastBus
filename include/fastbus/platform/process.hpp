/// @file process.hpp
/// @brief Child processes and process identity (M2.2, PRD_shm_transport.md section 4).
///
/// | Operation        | Windows                               | POSIX (Linux)                     |
/// |------------------|---------------------------------------|-----------------------------------|
/// | spawn            | CreateProcessW (UTF-16, quoted argv)  | posix_spawn                       |
/// | wait (timeout)   | WaitForSingleObject + GetExitCode...  | waitpid(WNOHANG) polling          |
/// | kill             | TerminateProcess(137)                 | kill(SIGKILL)                     |
/// | start time       | GetProcessTimes (creation FILETIME)   | /proc/<pid>/stat field 22         |
///
/// A pid alone does not identify a process: the OS reuses pids. `ProcessIdentity` adds the start
/// time, so "is the creator of this segment still alive?" cannot be fooled by a new process that
/// happens to get the same pid.
#pragma once

#include <chrono>
#include <cstdint>
#include <span>
#include <string_view>

#include "fastbus/core/result.hpp"

namespace fastbus::platform {

/// pid + start time. Trivially copyable fixed-width fields: it is stored in shared memory (M2.3).
struct ProcessIdentity {
    std::uint64_t pid;
    std::uint64_t start_time;  ///< opaque OS ticks; only compared for equality

    friend constexpr bool operator==(const ProcessIdentity&, const ProcessIdentity&) = default;
};

/// Exit code reported for a process killed with kill() (128 + SIGKILL, used on both OSes).
inline constexpr int kKilledExitCode = 137;

/// Identity of the calling process.
[[nodiscard]] ProcessIdentity current_process() noexcept;

/// Identity of a running process. Errors: NotFound (no such process, or already exited).
[[nodiscard]] core::Result<ProcessIdentity> identity_of(std::uint64_t pid) noexcept;

/// True if a process with this pid exists, has not exited, and started at `who.start_time`.
[[nodiscard]] bool is_alive(const ProcessIdentity& who) noexcept;

/// A child process. Move-only; destroying a still-running child kills and reaps it, so tests
/// never leave orphans behind.
class Process {
public:
    /// Starts `path` with `args` (argv[1..]; argv[0] is `path`). Strings are UTF-8.
    /// Errors: InvalidArgument, NotFound (no such executable), PermissionDenied, SystemError.
    [[nodiscard]] static core::Result<Process> spawn(
        std::string_view path, std::span<const std::string_view> args) noexcept;

    /// Waits up to `timeout` for the child to exit and returns its exit code (128 + signal if it
    /// was killed by a signal on POSIX). Errors: Timeout (still running), SystemError.
    /// After a successful wait the child is reaped; further waits return the same code.
    [[nodiscard]] core::Result<int> wait(std::chrono::milliseconds timeout) noexcept;

    /// Kills the child immediately (SIGKILL / TerminateProcess) and reaps it.
    [[nodiscard]] core::Result<void> kill() noexcept;

    /// True until the child has exited.
    [[nodiscard]] bool running() noexcept;

    [[nodiscard]] std::uint64_t pid() const noexcept { return pid_; }

    Process(Process&& other) noexcept;
    Process& operator=(Process&& other) noexcept;
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;
    ~Process();

private:
    Process() noexcept = default;
    void reset() noexcept;  // kill + reap if running, close handles

    std::uint64_t pid_ = 0;
    std::intptr_t handle_ = -1;  // Windows process HANDLE; unused on POSIX
    bool exited_ = false;
    int exit_code_ = 0;
};

}  // namespace fastbus::platform
