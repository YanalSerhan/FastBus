/// @file error.hpp
/// @brief Allocation-free error value used with `Result` (ADR-001).
#pragma once

#include <cstdint>
#include <string_view>

namespace fastbus::core {

/// Stable error categories. Values are part of logs and the shared registry, so append only;
/// never renumber or reuse a value. The 16-bit width is part of those formats, which is why it is
/// wider than the current value set needs.
enum class ErrorCode : std::uint16_t {  // NOLINT(performance-enum-size): fixed format width
    InvalidArgument = 1,                ///< an argument is outside its documented domain
    InvalidCapacity = 2,                ///< capacity is zero, not a power of two, or too large
    BufferTooSmall = 3,                 ///< caller-provided memory is smaller than required
    Misaligned = 4,                     ///< caller-provided memory is not suitably aligned
    OutOfMemory = 5,                    ///< an allocation at setup time failed
    NotSupported = 6,                   ///< the operation is not available on this platform/build
    SystemError = 7,                    ///< an OS call failed (details in Error::message)
};

/// Short, static, human-readable name of a code (e.g. "invalid capacity"). Never null.
[[nodiscard]] std::string_view to_string(ErrorCode code) noexcept;

/// An error: a code plus a static message giving context. Trivially copyable; `message` must
/// point to a string with static storage duration (a literal), so copying never allocates.
struct Error {
    ErrorCode code;
    const char* message;

    /// Errors compare equal when their codes do; the message is diagnostic only.
    friend constexpr bool operator==(const Error& a, const Error& b) noexcept {
        return a.code == b.code;
    }
};

}  // namespace fastbus::core
