/// @file ids.hpp
/// @brief The identifier types used across fastbus (TODO §3.4).
///
/// Widths are part of the shared-memory, wire and log formats; changing one is a format change
/// (bump the relevant layout/wire/format version, see docs/versions.md).
#pragma once

#include <cstdint>

#include "fastbus/core/strong_type.hpp"

namespace fastbus::core {

using TopicId = StrongType<std::uint32_t, struct TopicIdTag>;          ///< topic within a domain
using NodeId = StrongType<std::uint32_t, struct NodeIdTag>;            ///< participating node
using PublisherId = StrongType<std::uint32_t, struct PublisherIdTag>;  ///< publisher on a topic
using SequenceNumber = StrongType<std::uint64_t, struct SequenceNumberTag>;  ///< per publisher
using TypeId = StrongType<std::uint64_t, struct TypeIdTag>;  ///< FNV-1a of the type name (M4)
using SchemaVersion = StrongType<std::uint16_t, struct SchemaVersionTag>;  ///< @version(N) (M4)

/// The sequence number after `s`. Wraps from the maximum to 0 (unsigned arithmetic, no UB);
/// at one message per nanosecond a 64-bit counter wraps after ~584 years.
[[nodiscard]] constexpr SequenceNumber next(SequenceNumber s) noexcept {
    return SequenceNumber{s.value() + 1U};
}

}  // namespace fastbus::core
