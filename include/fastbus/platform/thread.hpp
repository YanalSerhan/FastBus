/// @file thread.hpp
/// @brief Thread placement and identification (M1.3). All functions act on the calling thread,
/// never allocate and never throw. Failures are reported as `false` because they are expected
/// in restricted environments (containers, CI runners, missing privileges), not bugs.
///
/// | Function                    | Windows                         | POSIX (Linux)              |
/// |-----------------------------|---------------------------------|----------------------------|
/// | pin_current_thread          | SetThreadAffinityMask           | pthread_setaffinity_np     |
/// | set_current_thread_name     | SetThreadDescription            | pthread_setname_np         |
/// | current_thread_name         | GetThreadDescription            | pthread_getname_np         |
/// | current_thread_id           | GetCurrentThreadId              | gettid                     |
/// | current_cpu                 | GetCurrentProcessorNumber       | sched_getcpu               |
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace fastbus::platform {

/// Longest thread name kept on every platform (Linux limit: 15 characters + NUL).
inline constexpr std::size_t kMaxThreadNameLength = 15;

/// Number of logical CPUs available to the process (at least 1).
[[nodiscard]] std::uint32_t hardware_concurrency() noexcept;

/// Pins the calling thread to logical CPU `cpu`. Returns false if the CPU does not exist or the
/// OS refuses (e.g. container CPU limits). Windows: only the first processor group (CPUs 0-63).
[[nodiscard]] bool pin_current_thread(std::uint32_t cpu) noexcept;

/// Removes any pinning: the thread may run on every CPU available to the process.
[[nodiscard]] bool unpin_current_thread() noexcept;

/// Names the calling thread for debuggers, profilers and `top -H`. Longer names are truncated to
/// kMaxThreadNameLength characters. Non-ASCII characters are replaced with '?'.
[[nodiscard]] bool set_current_thread_name(std::string_view name) noexcept;

/// Copies the calling thread's name into `out` (NUL-terminated) and returns its length, or 0 if
/// it cannot be read. `out` should hold at least kMaxThreadNameLength + 1 characters.
[[nodiscard]] std::size_t current_thread_name(std::span<char> out) noexcept;

/// OS identifier of the calling thread (stable for its lifetime, unique among live threads).
[[nodiscard]] std::uint64_t current_thread_id() noexcept;

/// Logical CPU the calling thread is running on right now (may change immediately unless pinned).
[[nodiscard]] std::uint32_t current_cpu() noexcept;

}  // namespace fastbus::platform
