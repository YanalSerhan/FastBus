#include "fastbus/core/message_header.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

#include "fastbus/core/error.hpp"
#include "fastbus/core/result.hpp"
#include "fastbus/platform/crc32c.hpp"

namespace fastbus::core {

Result<void> validate(const MessageHeader& header, std::size_t bytes_available) noexcept {
    if (header.magic != MessageHeader::kMagic) {
        return fail(ErrorCode::Corrupted, "message header: bad magic");
    }
    if (header.header_version != MessageHeader::kVersion) {
        return fail(ErrorCode::VersionMismatch, "message header: unknown header_version");
    }
    if ((header.flags & ~message_flags::kKnown) != 0) {
        return fail(ErrorCode::Corrupted, "message header: unknown flag bits");
    }
    if (bytes_available < sizeof(MessageHeader) ||
        header.payload_size > bytes_available - sizeof(MessageHeader)) {
        return fail(ErrorCode::Corrupted, "message header: payload_size exceeds the buffer");
    }
    return {};
}

std::uint32_t compute_checksum(const MessageHeader& header,
                               std::span<const std::byte> payload) noexcept {
    MessageHeader copy = header;
    copy.checksum = 0;
    const std::uint32_t over_header = platform::crc32c(std::as_bytes(std::span{&copy, 1}));
    return platform::crc32c(payload, over_header);
}

void seal(MessageHeader& header, std::span<const std::byte> payload) noexcept {
    header.flags = static_cast<std::uint16_t>(header.flags | message_flags::kChecksummed);
    header.checksum = compute_checksum(header, payload);
}

bool verify(const MessageHeader& header, std::span<const std::byte> payload) noexcept {
    if ((header.flags & message_flags::kChecksummed) == 0) {
        return true;
    }
    return compute_checksum(header, payload) == header.checksum;
}

}  // namespace fastbus::core
