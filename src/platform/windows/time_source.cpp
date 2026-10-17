// Windows backend of fastbus/platform/time_source.hpp (ADR-002: selected by cmake/Platform.cmake).
// WIN32_LEAN_AND_MEAN and NOMINMAX come from fastbus::platform_libs.
// clang-format off
#include <windows.h>  // must come first; timeapi.h is excluded by WIN32_LEAN_AND_MEAN
#include <timeapi.h>  // timeBeginPeriod / timeEndPeriod
// clang-format on

#include "fastbus/platform/time_source.hpp"

#include <algorithm>
#include <cstdint>

namespace fastbus::platform {
namespace {

constexpr std::int64_t kNsPerSecond = 1'000'000'000;
constexpr std::int64_t kNsPer100ns = 100;
// 100-ns intervals between 1601-01-01 (FILETIME epoch) and 1970-01-01 (Unix epoch).
constexpr std::int64_t kFileTimeToUnixEpoch = 116'444'736'000'000'000;

std::int64_t qpc_frequency() noexcept {
    // Fixed at boot; reading it once is documented as safe. Function-local static: thread-safe.
    static const std::int64_t frequency = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return static_cast<std::int64_t>(f.QuadPart);
    }();
    return frequency;
}

}  // namespace

std::int64_t monotonic_now_ns() noexcept {
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    const std::int64_t ticks = counter.QuadPart;
    const std::int64_t freq = qpc_frequency();
    // Split to avoid overflow: ticks * 1e9 would overflow int64 after ~15 minutes at 10 MHz.
    return ((ticks / freq) * kNsPerSecond) + (((ticks % freq) * kNsPerSecond) / freq);
}

std::int64_t wall_now_ns() noexcept {
    FILETIME ft{};
    GetSystemTimePreciseAsFileTime(&ft);
    ULARGE_INTEGER t{};
    t.LowPart = ft.dwLowDateTime;
    t.HighPart = ft.dwHighDateTime;
    return (static_cast<std::int64_t>(t.QuadPart) - kFileTimeToUnixEpoch) * kNsPer100ns;
}

std::int64_t monotonic_resolution_ns() noexcept {
    const std::int64_t freq = qpc_frequency();
    return std::max<std::int64_t>(1, (kNsPerSecond + freq - 1) / freq);
}

void sleep_for_ns(std::int64_t ns) noexcept {
    if (ns <= 0) {
        return;
    }
    // A high-resolution waitable timer (Windows 10 1803+) sleeps with ~0.5 ms precision without
    // changing the machine-wide timer resolution. Fall back to Sleep() if it is unavailable.
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                          TIMER_ALL_ACCESS);
    if (timer != nullptr) {
        LARGE_INTEGER due{};
        due.QuadPart = -((ns + kNsPer100ns - 1) / kNsPer100ns);  // negative = relative, 100 ns
        if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE) != 0) {
            WaitForSingleObject(timer, INFINITE);
            CloseHandle(timer);
            return;
        }
        CloseHandle(timer);
    }
    constexpr std::int64_t kNsPerMs = 1'000'000;
    constexpr std::int64_t kMaxMs = 0x7FFF'FFFF;
    const std::int64_t ms = std::min((ns + kNsPerMs - 1) / kNsPerMs, kMaxMs);
    Sleep(static_cast<DWORD>(ms));
}

HighResolutionTimerScope::HighResolutionTimerScope() noexcept
    : active_{timeBeginPeriod(1) == TIMERR_NOERROR} {}

HighResolutionTimerScope::~HighResolutionTimerScope() {
    if (active_) {
        timeEndPeriod(1);
    }
}

}  // namespace fastbus::platform
