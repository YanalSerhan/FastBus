// Segment header validation and stale-segment removal (M2.3). Shared memory is untrusted:
// every field is checked before anything relies on it (PRD_shm_transport.md T6).
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

#include "fastbus/core/error.hpp"
#include "fastbus/core/result.hpp"
#include "fastbus/platform/process.hpp"
#include "fastbus/platform/shared_memory.hpp"
#include "fastbus/transport/segment.hpp"

#include "segment_internal.hpp"

namespace fastbus::transport {
namespace {

constexpr std::size_t kHeaderSize = sizeof(SegmentHeader);

template <typename T>
T read_at(std::span<const std::byte> bytes, std::size_t offset) noexcept {
    T value{};
    std::memcpy(&value, bytes.subspan(offset, sizeof(T)).data(), sizeof(T));
    return value;
}

}  // namespace

core::Result<void> validate_segment(std::span<const std::byte> mapping) noexcept {
    if (mapping.size() < kHeaderSize) {
        return core::fail(core::ErrorCode::Corrupted, "segment smaller than its header");
    }
    if (read_at<std::uint64_t>(mapping, offsetof(SegmentHeader, magic)) != SegmentHeader::kMagic) {
        return core::fail(core::ErrorCode::Corrupted, "segment: bad magic");
    }
    if (read_at<std::uint16_t>(mapping, offsetof(SegmentHeader, layout_version)) !=
        SegmentHeader::kLayoutVersion) {
        return core::fail(core::ErrorCode::VersionMismatch, "segment: unknown layout_version");
    }
    const auto header_size = read_at<std::uint16_t>(mapping, offsetof(SegmentHeader, header_size));
    const auto total = read_at<std::uint64_t>(mapping, offsetof(SegmentHeader, total_size));
    const auto offset = read_at<std::uint64_t>(mapping, offsetof(SegmentHeader, payload_offset));
    const auto size = read_at<std::uint64_t>(mapping, offsetof(SegmentHeader, payload_size));
    if (header_size != kHeaderSize || total < kHeaderSize || total > mapping.size() ||
        offset < kHeaderSize || offset % SegmentHeader::kPayloadAlignment != 0 || offset > total ||
        size > total - offset) {
        return core::fail(core::ErrorCode::Corrupted, "segment: inconsistent sizes or offsets");
    }
    return {};
}

namespace segment_detail {

core::Result<void> remove_if_stale(std::string_view name) noexcept {
    auto existing = platform::SharedMemory::open(name);
    if (!existing) {
        return core::Unexpected{existing.error()};
    }
    if (existing->size() < kHeaderSize) {
        return core::fail(core::ErrorCode::Corrupted, "existing object is not a fastbus segment");
    }
    auto& h = *reinterpret_cast<SegmentHeader*>(existing->data());
    const std::uint64_t magic = load_relaxed(h.magic);
    const std::uint32_t state = h.state.load(std::memory_order_acquire);
    if (magic != SegmentHeader::kMagic) {
        if (magic == 0 && state == 0) {
            return core::fail(core::ErrorCode::AlreadyExists, "segment is being created right now");
        }
        return core::fail(core::ErrorCode::Corrupted,
                          "existing object is not a fastbus segment (remove it with fbclean)");
    }
    if (platform::is_alive(creator_of(h))) {
        return core::fail(core::ErrorCode::AlreadyExists,
                          "segment exists and its creator is alive");
    }
    const auto removed = platform::SharedMemory::remove(name);
    if (removed || removed.error().code == core::ErrorCode::NotFound) {
        return {};
    }
    if (removed.error().code == core::ErrorCode::NotSupported) {
        return core::fail(core::ErrorCode::AlreadyExists,
                          "stale segment (creator dead) is still mapped by another process");
    }
    return core::Unexpected{removed.error()};
}

}  // namespace segment_detail
}  // namespace fastbus::transport
