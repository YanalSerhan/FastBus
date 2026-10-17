/// @file time_source.hpp
/// @brief Raw OS time reads and sleeping (ADR-007). The only place that talks to OS clocks.
///
/// Higher layers use `fastbus::core::Clock` (fastbus/core/clock.hpp), never these directly.
/// Values are plain nanoseconds as int64 so this layer stays independent of `core` types.
///
/// | Function             | Windows                          | POSIX                       |
/// |----------------------|----------------------------------|-----------------------------|
/// | monotonic_now_ns     | QueryPerformanceCounter          | clock_gettime(MONOTONIC)    |
/// | wall_now_ns          | GetSystemTimePreciseAsFileTime   | clock_gettime(REALTIME)     |
/// | sleep_for_ns         | high-resolution waitable timer   | nanosleep (EINTR-safe)      |
///
/// The monotonic clock is system-wide on both OSes (all processes read the same timeline),
/// which cross-process latency measurement relies on. All functions are thread-safe.
#pragma once

#include <cstdint>

namespace fastbus::platform {

/// Nanoseconds since an unspecified, system-wide, boot-relative epoch. Never goes backwards.
[[nodiscard]] std::int64_t monotonic_now_ns() noexcept;

/// Nanoseconds since 1970-01-01T00:00:00Z (UTC). May jump when the system time is adjusted.
[[nodiscard]] std::int64_t wall_now_ns() noexcept;

/// Tick length of the monotonic clock in nanoseconds (>= 1).
[[nodiscard]] std::int64_t monotonic_resolution_ns() noexcept;

/// Blocks the calling thread for at least `ns` nanoseconds (returns immediately if ns <= 0).
/// Granularity: ~0.5-1 ms on Windows 10 1803+ (high-resolution timer), better on Linux.
void sleep_for_ns(std::int64_t ns) noexcept;

/// RAII opt-in to 1 ms system timer resolution (Windows `timeBeginPeriod(1)`); no-op elsewhere.
/// This changes a machine-wide setting and costs power, so it is never enabled implicitly; use it
/// only around code that needs precise sleeps (e.g. real-time replay in M5).
class HighResolutionTimerScope {
public:
    HighResolutionTimerScope() noexcept;
    // Out of line on purpose: calls timeEndPeriod on Windows (trivial only on POSIX).
    ~HighResolutionTimerScope();  // NOLINT(performance-trivially-destructible)
    HighResolutionTimerScope(const HighResolutionTimerScope&) = delete;
    HighResolutionTimerScope& operator=(const HighResolutionTimerScope&) = delete;
    HighResolutionTimerScope(HighResolutionTimerScope&&) = delete;
    HighResolutionTimerScope& operator=(HighResolutionTimerScope&&) = delete;

    /// True if the OS accepted the request (always false on non-Windows).
    [[nodiscard]] bool active() const noexcept { return active_; }

private:
    bool active_{false};
};

}  // namespace fastbus::platform
