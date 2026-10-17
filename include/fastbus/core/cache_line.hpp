/// @file cache_line.hpp
/// @brief Fixed cache-line constants (ADR-009). These are ABI: shared-memory layouts static_assert
/// against them, so they are the same on every compiler and target. Do not replace them with
/// std::hardware_destructive_interference_size (varies with -mtune, GCC warns in headers).
#pragma once

#include <cstddef>

namespace fastbus::core {

/// Alignment of shared structures and slot arrays (x86-64 and most ARM64 line size).
inline constexpr std::size_t kCacheLineSize = 64;

/// Spacing between fields written by different threads. Two lines, because x86's spatial
/// prefetcher fetches 64-byte lines in 128-byte pairs, and some ARM64 cores have 128-byte lines.
inline constexpr std::size_t kFalseSharingRange = 128;

static_assert(kFalseSharingRange % kCacheLineSize == 0);
static_assert((kCacheLineSize & (kCacheLineSize - 1)) == 0, "must be a power of two");

}  // namespace fastbus::core
