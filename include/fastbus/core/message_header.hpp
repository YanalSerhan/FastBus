/// @file message_header.hpp
/// @brief The 64-byte header in front of every message: in shared memory, on the wire (M6) and
/// in log files (M5). Layout rules: ADR-004. Checksum: ADR-005. Table: PRD_shm_transport 6a.
#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

#include "fastbus/core/cache_line.hpp"
#include "fastbus/core/result.hpp"

namespace fastbus::core {

/// 64-bit FNV-1a: the message type id is this hash of the fully qualified type name.
[[nodiscard]] constexpr std::uint64_t fnv1a_64(std::string_view text) noexcept {
    std::uint64_t hash = 0xCBF2'9CE4'8422'2325ULL;  // offset basis
    for (const char c : text) {
        hash ^= static_cast<std::uint8_t>(c);
        hash *= 0x0000'0100'0000'01B3ULL;  // FNV prime
    }
    return hash;
}

namespace message_flags {
inline constexpr std::uint16_t kReplayed = 1U << 0U;     ///< produced by fbplay (M5)
inline constexpr std::uint16_t kChecksummed = 1U << 1U;  ///< `checksum` is valid
inline constexpr std::uint16_t kKnown = kReplayed | kChecksummed;
}  // namespace message_flags

struct alignas(kCacheLineSize) MessageHeader {
    static constexpr std::uint32_t kMagic = 0x534D'4246U;  // "FBMS" in little-endian bytes
    static constexpr std::uint16_t kVersion = 1;

    std::uint32_t magic;
    std::uint16_t header_version;
    std::uint16_t schema_version;
    std::uint64_t type_id;
    std::uint64_t sequence;
    std::int64_t timestamp_ns;
    std::uint32_t payload_size;
    std::uint32_t checksum;
    std::uint16_t flags;
    std::array<std::uint8_t, 22> reserved;  ///< always zero on write (ADR-004 rule 3)
};

// ADR-004: the layout is a format. Any change here is a header_version bump.
static_assert(std::endian::native == std::endian::little);
static_assert(sizeof(MessageHeader) == 64 && alignof(MessageHeader) == 64);
static_assert(std::is_trivially_copyable_v<MessageHeader> &&
              std::is_standard_layout_v<MessageHeader>);
static_assert(offsetof(MessageHeader, magic) == 0);
static_assert(offsetof(MessageHeader, header_version) == 4);
static_assert(offsetof(MessageHeader, schema_version) == 6);
static_assert(offsetof(MessageHeader, type_id) == 8);
static_assert(offsetof(MessageHeader, sequence) == 16);
static_assert(offsetof(MessageHeader, timestamp_ns) == 24);
static_assert(offsetof(MessageHeader, payload_size) == 32);
static_assert(offsetof(MessageHeader, checksum) == 36);
static_assert(offsetof(MessageHeader, flags) == 40);
static_assert(offsetof(MessageHeader, reserved) == 42);

/// A fully initialized header (reserved bytes zero, no checksum).
[[nodiscard]] constexpr MessageHeader make_header(std::uint64_t type_id,
                                                  std::uint16_t schema_version,
                                                  std::uint64_t sequence, std::int64_t timestamp_ns,
                                                  std::uint32_t payload_size,
                                                  std::uint16_t flags = 0) noexcept {
    return MessageHeader{.magic = MessageHeader::kMagic,
                         .header_version = MessageHeader::kVersion,
                         .schema_version = schema_version,
                         .type_id = type_id,
                         .sequence = sequence,
                         .timestamp_ns = timestamp_ns,
                         .payload_size = payload_size,
                         .checksum = 0,
                         .flags = static_cast<std::uint16_t>(flags & ~message_flags::kChecksummed),
                         .reserved = {}};
}

/// Checks a header read from untrusted memory: magic, version, flags, and that the payload fits
/// in `bytes_available` (the bytes from the start of the header to the end of the buffer).
/// Errors: Corrupted, VersionMismatch.
[[nodiscard]] Result<void> validate(const MessageHeader& header,
                                    std::size_t bytes_available) noexcept;

/// CRC32C over the header (with `checksum` = 0) followed by `payload` (ADR-005).
[[nodiscard]] std::uint32_t compute_checksum(const MessageHeader& header,
                                             std::span<const std::byte> payload) noexcept;

/// Sets `checksummed` and `checksum` for this payload.
void seal(MessageHeader& header, std::span<const std::byte> payload) noexcept;

/// True if the header is not checksummed, or its checksum matches the payload.
[[nodiscard]] bool verify(const MessageHeader& header, std::span<const std::byte> payload) noexcept;

}  // namespace fastbus::core
