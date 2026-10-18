/// @file segment.hpp
/// @brief A shared-memory segment with a validated header and a lifecycle (M2.3,
/// PRD_shm_transport.md section 6, memory-ordering.md section 14).
///
/// Creator:  create() -> (lay out the payload) -> mark_ready() -> ... -> ~Segment (closing)
/// Opener:   open() waits for `ready`, validates the header, checks the creator is alive.
///
/// Stale segments (creator dead, name still present) are detected by pid + start time and
/// recreated by create(); the payload of a segment is never trusted beyond what validation proved.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

#include "fastbus/core/cache_line.hpp"
#include "fastbus/core/result.hpp"
#include "fastbus/core/time.hpp"
#include "fastbus/platform/process.hpp"
#include "fastbus/platform/shared_memory.hpp"

namespace fastbus::transport {

// NOLINTNEXTLINE(performance-enum-size): mirrors the u32 `state` field in shared memory
enum class SegmentState : std::uint32_t { Initializing = 0, Ready = 1, Closing = 2 };

namespace segment_kind {
inline constexpr std::uint32_t kTopic = 1;      ///< ShmTransport topic segment (M2.7)
inline constexpr std::uint32_t kTest = 0xFFFF;  ///< tests and tools
}  // namespace segment_kind

/// First 128 bytes of every segment (ADR-004). Written once by the creator before `ready`.
struct alignas(core::kCacheLineSize) SegmentHeader {
    static constexpr std::uint64_t kMagic = 0x544E'4D47'4553'4246ULL;  // "FBSEGMNT"
    static constexpr std::uint16_t kLayoutVersion = 1;
    static constexpr std::size_t kPayloadAlignment = core::kCacheLineSize;

    std::uint64_t magic{};
    std::uint16_t layout_version{};
    std::uint16_t header_size{};
    std::uint32_t kind{};
    std::uint64_t total_size{};      ///< exact bytes in use, <= mapping size
    std::uint64_t payload_offset{};  ///< from the segment base, multiple of kPayloadAlignment
    std::uint64_t payload_size{};
    platform::ProcessIdentity creator{};
    std::uint64_t epoch{};  ///< wall-clock ns at creation: tells two lives of one name apart
    std::atomic<std::uint32_t> state{0};
    std::array<std::uint8_t, 60> reserved{};
};
static_assert(sizeof(SegmentHeader) == 128 && alignof(SegmentHeader) == 64);
static_assert(std::is_standard_layout_v<SegmentHeader>);
static_assert(std::is_trivially_destructible_v<SegmentHeader>);
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
static_assert(offsetof(SegmentHeader, kind) == 12 && offsetof(SegmentHeader, total_size) == 16);
static_assert(offsetof(SegmentHeader, payload_offset) == 24);
static_assert(offsetof(SegmentHeader, payload_size) == 32);
static_assert(offsetof(SegmentHeader, creator) == 40 && offsetof(SegmentHeader, epoch) == 56);
static_assert(offsetof(SegmentHeader, state) == 64);

/// Checks a *ready* segment's header against the mapping it lives in: magic, version, sizes,
/// offsets, alignment. Errors: Corrupted, VersionMismatch. Does not check liveness.
[[nodiscard]] core::Result<void> validate_segment(std::span<const std::byte> mapping) noexcept;

class Segment {
public:
    /// Creates `name` with room for `payload_size` bytes, in state `initializing`. If the name
    /// exists and its creator is dead, the stale segment is removed and created again.
    /// Errors: InvalidArgument, AlreadyExists (live creator, or stale but still mapped elsewhere
    /// on Windows), Corrupted (the name holds something that is not a fastbus segment), and the
    /// SharedMemory errors.
    [[nodiscard]] static core::Result<Segment> create(std::string_view name,
                                                      std::size_t payload_size,
                                                      std::uint32_t kind) noexcept;

    /// Opens `name`, waiting up to `timeout` for it to become ready. Errors: NotFound (missing,
    /// closing, or its creator is dead), Timeout, Corrupted, VersionMismatch.
    [[nodiscard]] static core::Result<Segment> open(std::string_view name,
                                                    core::Duration timeout) noexcept;

    /// Creator only: publishes the header and everything written to payload() so far [G1].
    void mark_ready() noexcept;

    [[nodiscard]] const SegmentHeader& header() const noexcept { return *header_; }
    [[nodiscard]] SegmentState state() const noexcept;
    [[nodiscard]] std::span<std::byte> payload() const noexcept { return payload_; }
    [[nodiscard]] bool is_creator() const noexcept { return shm_.is_creator(); }
    [[nodiscard]] std::string_view name() const noexcept { return shm_.name(); }

    Segment(Segment&& other) noexcept;
    Segment& operator=(Segment&& other) noexcept;
    Segment(const Segment&) = delete;
    Segment& operator=(const Segment&) = delete;
    ~Segment();  ///< creator: marks the segment closing [G3] before unmapping

private:
    Segment(platform::SharedMemory shm, SegmentHeader* header,
            std::span<std::byte> payload) noexcept;
    void mark_closing() noexcept;

    platform::SharedMemory shm_;
    SegmentHeader* header_ = nullptr;
    std::span<std::byte> payload_;
};

}  // namespace fastbus::transport
