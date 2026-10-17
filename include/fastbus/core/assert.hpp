/// @file assert.hpp
/// @brief Contract checks that never throw (ADR-001).
///
/// `fail_fast` prints a message and aborts. It is used for precondition violations that cannot be
/// reported through `Result` (e.g. calling `Result::value()` on an error). `kDebugChecks` lets
/// code compile expensive or hot-path checks out of release builds with `if constexpr`.
#pragma once

#include <source_location>

namespace fastbus::core {

#ifdef NDEBUG
inline constexpr bool kDebugChecks = false;
#else
inline constexpr bool kDebugChecks = true;
#endif

/// Writes "fastbus: fatal: <what> (<file>:<line>)" to stderr and calls std::abort().
/// Thread-safety: safe from any thread. Never returns, never throws, never allocates.
[[noreturn]] void fail_fast(const char* what,
                            std::source_location where = std::source_location::current()) noexcept;

/// Calls fail_fast(what) when `condition` is false, in every build type.
constexpr void check(bool condition, const char* what,
                     std::source_location where = std::source_location::current()) noexcept {
    if (!condition) [[unlikely]] {
        fail_fast(what, where);
    }
}

/// Like check(), but compiled out when NDEBUG is defined (release builds).
constexpr void debug_check(bool condition, const char* what,
                           std::source_location where = std::source_location::current()) noexcept {
    if constexpr (kDebugChecks) {
        check(condition, what, where);
    }
}

}  // namespace fastbus::core
