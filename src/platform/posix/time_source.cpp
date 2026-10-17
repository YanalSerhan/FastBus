// POSIX backend of fastbus/platform/time_source.hpp (ADR-002: selected by cmake/Platform.cmake).
#include "fastbus/platform/time_source.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <ctime>

namespace fastbus::platform {
namespace {

constexpr std::int64_t kNsPerSecond = 1'000'000'000;

std::int64_t read_clock(clockid_t id) noexcept {
    timespec ts{};
    // Only fails for an invalid clock id or bad pointer, i.e. a programming error.
    if (clock_gettime(id, &ts) != 0) {
        std::abort();
    }
    return (static_cast<std::int64_t>(ts.tv_sec) * kNsPerSecond) + ts.tv_nsec;
}

}  // namespace

std::int64_t monotonic_now_ns() noexcept {
    return read_clock(CLOCK_MONOTONIC);
}

std::int64_t wall_now_ns() noexcept {
    return read_clock(CLOCK_REALTIME);
}

std::int64_t monotonic_resolution_ns() noexcept {
    timespec res{};
    if (clock_getres(CLOCK_MONOTONIC, &res) != 0) {
        return 1;
    }
    const std::int64_t ns = (static_cast<std::int64_t>(res.tv_sec) * kNsPerSecond) + res.tv_nsec;
    return ns > 0 ? ns : 1;
}

void sleep_for_ns(std::int64_t ns) noexcept {
    if (ns <= 0) {
        return;
    }
    timespec remaining{};
    remaining.tv_sec = static_cast<time_t>(ns / kNsPerSecond);
    remaining.tv_nsec = static_cast<long>(ns % kNsPerSecond);
    // nanosleep stores the unslept time in `remaining` when a signal interrupts it.
    while (nanosleep(&remaining, &remaining) == -1 && errno == EINTR) {
    }
}

HighResolutionTimerScope::HighResolutionTimerScope() noexcept = default;
HighResolutionTimerScope::~HighResolutionTimerScope() = default;

}  // namespace fastbus::platform
