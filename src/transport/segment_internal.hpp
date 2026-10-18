// Private to the segment implementation (segment.cpp, segment_validate.cpp).
#pragma once

#include <atomic>
#include <cstdint>
#include <string_view>

#include "fastbus/core/result.hpp"
#include "fastbus/platform/process.hpp"
#include "fastbus/transport/segment.hpp"

namespace fastbus::transport::segment_detail {

// Fields an opener may need while the segment is still initializing are written and read with
// atomic_ref (relaxed): the creator may be writing them at the same moment.
inline std::uint64_t load_relaxed(std::uint64_t& field) noexcept {
    return std::atomic_ref<std::uint64_t>{field}.load(std::memory_order_relaxed);
}
inline void store_relaxed(std::uint64_t& field, std::uint64_t value) noexcept {
    std::atomic_ref<std::uint64_t>{field}.store(value, std::memory_order_relaxed);
}
inline platform::ProcessIdentity creator_of(SegmentHeader& h) noexcept {
    return {.pid = load_relaxed(h.creator.pid), .start_time = load_relaxed(h.creator.start_time)};
}

/// The name exists. Decide whether it is a stale segment (creator dead) and remove it.
/// Success = removed (retry the create). NotFound = it vanished meanwhile (retry too).
[[nodiscard]] core::Result<void> remove_if_stale(std::string_view name) noexcept;

}  // namespace fastbus::transport::segment_detail
