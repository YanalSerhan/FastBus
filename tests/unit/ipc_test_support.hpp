// Shared helpers for tests that use shared memory or child processes.
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>

#include "fastbus/core/error.hpp"
#include "fastbus/platform/process.hpp"
#include "fastbus/platform/time_source.hpp"

#if !defined(FASTBUS_TEST_CHILD)
#error "FASTBUS_TEST_CHILD must be defined (fastbus_use_test_child in tests/helpers)"
#endif

namespace fastbus::test {

/// Path of the fb_test_child helper executable.
inline constexpr std::string_view kTestChild = FASTBUS_TEST_CHILD;

/// A shared-memory name unique to this process and call: tests run in parallel (one process
/// per test case), so names must never collide.
inline std::string unique_shm_name(std::string_view tag) {
    static std::atomic<int> counter{0};
    return "fbtest." + std::string{tag} + "." + std::to_string(platform::current_process().pid) +
           "." + std::to_string(counter++);
}

/// The error code of a failed Result, or ErrorCode{0} ("no error") if it succeeded. Use it in
/// assertions instead of `.error().code`: an unexpected success then fails the assertion with a
/// readable message instead of aborting the whole test binary in Result::error().
template <typename R>
core::ErrorCode error_of(const R& result) {
    return result.has_value() ? core::ErrorCode{0} : result.error().code;
}

/// Polls `condition` every millisecond for up to `timeout_ms`. Returns its last value.
template <typename F>
bool eventually(F condition, std::int64_t timeout_ms = 5'000) {
    const std::int64_t deadline = platform::monotonic_now_ns() + (timeout_ms * 1'000'000);
    while (!condition()) {
        if (platform::monotonic_now_ns() >= deadline) {
            return condition();
        }
        platform::sleep_for_ns(1'000'000);
    }
    return true;
}

}  // namespace fastbus::test
