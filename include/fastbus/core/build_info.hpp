/// @file build_info.hpp
/// @brief Version of the *compiled* fastbus library, for header/library mismatch detection.
///
/// `fastbus::version()` (fastbus/version.hpp) is what the caller's headers say;
/// `fastbus::core::library_version()` is what the linked library was built as. Tools print both,
/// and a mismatch means stale headers or a stale library on the include/link path.
#pragma once

#include "fastbus/version.hpp"

namespace fastbus::core {

/// Returns the version the fastbus core library was compiled with.
/// Thread-safety: safe to call from any thread. Never fails, never allocates.
[[nodiscard]] VersionInfo library_version() noexcept;

/// Returns true when the headers in use match the linked library exactly.
[[nodiscard]] bool headers_match_library() noexcept;

}  // namespace fastbus::core
