/// @file slab.hpp
/// @brief Fixed-size block pool for zero-copy fan-out (M2.5, ADR-003, PRD_shm_transport 7).
///
/// A view over caller memory (the segment payload in M2.7), with no pointers inside:
///   [ SlabHeader | free stack u32[block_count] | block 0 | block 1 | ... ]
///   block = [ BlockHeader (atomic<u64> state = generation << 32 | low) | payload ]
///
/// The publisher (the view made by `create`) is the only allocator; subscribers (`attach`) only
/// read payloads and release references. Ordering argument: docs/memory-ordering.md section 13.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

#include "fastbus/core/cache_line.hpp"
#include "fastbus/core/result.hpp"

namespace fastbus::core {

/// Names one block *in one life*: the generation changes every time the block is freed, so a
/// handle kept past that point is detected as stale.
struct BlockHandle {
    std::uint32_t index;
    std::uint32_t generation;
    friend constexpr bool operator==(BlockHandle, BlockHandle) noexcept = default;
};

enum class ReleaseResult : std::uint8_t {
    Stale,      ///< wrong generation, not shared, or already fully released: nothing changed
    Remaining,  ///< reference dropped; others still hold the block
    Last,       ///< last reference dropped: hand the index back to the publisher (return ring)
};

struct alignas(kCacheLineSize) SlabHeader {
    static constexpr std::uint32_t kMagic = 0x4C53'4246U;  // "FBSL"
    static constexpr std::uint32_t kVersion = 1;
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t block_count;
    std::uint32_t block_stride;       ///< bytes per block, BlockHeader included, multiple of 64
    std::uint64_t payload_capacity;   ///< usable payload bytes per block
    std::uint64_t free_stack_offset;  ///< from the start of the slab memory
    std::uint64_t blocks_offset;
    std::uint32_t free_top;  ///< publisher-only: number of entries on the free stack
    std::array<std::uint8_t, 20> reserved;
};
static_assert(sizeof(SlabHeader) == 64 && std::is_trivially_copyable_v<SlabHeader>);

struct alignas(kCacheLineSize) BlockHeader {
    std::atomic<std::uint64_t> state{0};  ///< generation << 32 | low (FREE, LOANED, refs, RETURNED)
    std::array<std::uint8_t, 56> reserved{};
};
static_assert(sizeof(BlockHeader) == 64 && std::atomic<std::uint64_t>::is_always_lock_free);

class SlabView {
public:
    static constexpr std::uint32_t kMaxRefs = 0xFFFF'FFFDU;  ///< largest share() count

    /// Bytes `create` needs, or 0 if the parameters are invalid or the size overflows.
    [[nodiscard]] static std::size_t required_bytes(std::uint32_t block_count,
                                                    std::size_t payload_capacity) noexcept;

    /// Lays out a new slab with every block FREE. Errors: InvalidArgument, BufferTooSmall,
    /// Misaligned.
    [[nodiscard]] static Result<SlabView> create(std::span<std::byte> memory,
                                                 std::uint32_t block_count,
                                                 std::size_t payload_capacity) noexcept;

    /// Opens an existing slab (subscriber side) after validating its header against `memory`.
    /// Errors: Corrupted, VersionMismatch, BufferTooSmall, Misaligned.
    [[nodiscard]] static Result<SlabView> attach(std::span<std::byte> memory) noexcept;

    // ---- Publisher only -------------------------------------------------------------------
    /// FREE -> LOANED. Errors: PoolExhausted (no free block; never blocks), Corrupted.
    [[nodiscard]] Result<BlockHandle> allocate() noexcept;
    /// LOANED -> SHARED(refs), 1 <= refs <= kMaxRefs. False if the handle is not a live loan.
    [[nodiscard]] bool share(BlockHandle handle, std::uint32_t refs) noexcept;
    /// LOANED -> FREE: a loan dropped without publishing. False if not a live loan.
    [[nodiscard]] bool free(BlockHandle handle) noexcept;
    /// RETURNED -> FREE for an index that came back through a return ring. False (and nothing
    /// changes) if the index is out of range or the block is not RETURNED.
    [[nodiscard]] bool reclaim(std::uint32_t index) noexcept;

    // ---- Any side -------------------------------------------------------------------------
    /// SHARED(r) -> SHARED(r - 1) or RETURNED. Lock-free; safe from any process.
    [[nodiscard]] ReleaseResult release(BlockHandle handle) noexcept;
    /// The block's payload, or an empty span if the handle is stale or out of range.
    [[nodiscard]] std::span<std::byte> payload(BlockHandle handle) const noexcept;

    [[nodiscard]] std::uint32_t capacity() const noexcept { return block_count_; }
    [[nodiscard]] std::size_t payload_capacity() const noexcept { return payload_capacity_; }
    [[nodiscard]] std::uint32_t free_count() const noexcept;  ///< publisher view

private:
    SlabView() noexcept = default;
    [[nodiscard]] BlockHeader* block(std::uint32_t index) const noexcept;
    void push_free(std::uint32_t index) noexcept;

    // Validated copies: never re-read from shared memory after create/attach (untrusted).
    SlabHeader* header_ = nullptr;
    std::span<std::uint32_t> free_stack_;
    std::byte* blocks_ = nullptr;
    std::uint32_t block_count_ = 0;
    std::uint32_t block_stride_ = 0;
    std::size_t payload_capacity_ = 0;
    bool publisher_ = false;
};

}  // namespace fastbus::core
