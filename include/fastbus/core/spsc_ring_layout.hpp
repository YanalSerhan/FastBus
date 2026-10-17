/// @file spsc_ring_layout.hpp
/// @brief Memory layout of the SPSC ring (M1.4): the header placed at the start of the ring's
/// memory, the capacity rules and the counter arithmetic. No pointers inside, so the same bytes
/// can live in shared memory (M2). The protocol on top of this layout is in
/// docs/memory-ordering.md; tags like [P3] refer to its section 4.
#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <concepts>
#include <cstddef>
#include <limits>
#include <type_traits>

#include "fastbus/core/cache_line.hpp"

namespace fastbus::core {

/// Element types the ring can carry: plain bytes that are valid in shared memory.
template <typename T>
concept RingElement = std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T> &&
                      !std::is_const_v<T> && !std::is_volatile_v<T> && alignof(T) <= kCacheLineSize;

/// Counter types: unsigned, not bool, and lock-free as atomics (required across processes).
template <typename IndexT>
concept RingIndex = std::unsigned_integral<IndexT> && !std::same_as<IndexT, bool> &&
                    std::atomic<IndexT>::is_always_lock_free;

namespace detail {
/// Fills the rest of a kFalseSharingRange block after `Used` bytes of fields.
template <std::size_t Used>
using BlockPadding = std::array<std::byte, kFalseSharingRange - Used>;
}  // namespace detail

/// Header at offset 0 of the ring memory: four kFalseSharingRange blocks (memory-ordering.md §2).
/// Constructed once in place by the ring's create(); never copied or moved.
template <RingIndex IndexT>
struct alignas(kCacheLineSize) SpscRingHeader {
    /// `start` is the initial value of both counters (0 normally; tests start near the wrap point).
    constexpr explicit SpscRingHeader(IndexT mask, IndexT start = 0) noexcept
        : tail(start),
          cached_head(start),
          producer_mask(mask),
          head(start),
          cached_tail(start),
          consumer_mask(mask) {}

    // Block 0: written by the producer on every push [P3], read by the consumer on refresh [C2].
    std::atomic<IndexT> tail;
    detail::BlockPadding<sizeof(std::atomic<IndexT>)> tail_padding{};

    // Block 1: producer-only (plain fields, refreshed by [P2]).
    IndexT cached_head;
    IndexT producer_mask;
    detail::BlockPadding<2 * sizeof(IndexT)> producer_padding{};

    // Block 2: written by the consumer on every pop [C3], read by the producer on refresh [P2].
    std::atomic<IndexT> head;
    detail::BlockPadding<sizeof(std::atomic<IndexT>)> head_padding{};

    // Block 3: consumer-only (plain fields, refreshed by [C2]).
    IndexT cached_tail;
    IndexT consumer_mask;
    detail::BlockPadding<2 * sizeof(IndexT)> consumer_padding{};
};

/// Steps from counter `from` to counter `to`, modulo 2^bits(IndexT).
template <RingIndex IndexT>
[[nodiscard]] constexpr IndexT ring_distance(IndexT from, IndexT to) noexcept {
    return static_cast<IndexT>(to - from);  // cast undoes integer promotion for 8/16-bit types
}

/// The counter after `i` (wraps to 0).
template <RingIndex IndexT>
[[nodiscard]] constexpr IndexT ring_next(IndexT i) noexcept {
    return static_cast<IndexT>(i + 1U);
}

/// Slot index of counter `i` for a ring with `mask = capacity - 1`.
template <RingIndex IndexT>
[[nodiscard]] constexpr std::size_t ring_slot(IndexT i, IndexT mask) noexcept {
    return static_cast<std::size_t>(i & mask);
}

/// Size, alignment and capacity rules for a ring of `T` with `IndexT` counters:
/// [ header (4 x 128 bytes) | T[capacity] ].
template <RingElement T, RingIndex IndexT>
struct SpscRingLayout {
    using Header = SpscRingHeader<IndexT>;

    /// Bytes from the start of the ring memory to slot 0.
    static constexpr std::size_t kSlotsOffset = sizeof(Header);
    /// Alignment the caller's memory must have.
    static constexpr std::size_t kRequiredAlignment = kCacheLineSize;
    /// Largest capacity that keeps full and empty distinguishable: half the index range
    /// (memory-ordering.md section 9).
    static constexpr std::size_t kMaxCapacity = std::size_t{1}
                                                << (std::numeric_limits<IndexT>::digits - 1);

    /// True if `capacity` is a power of two in [2, kMaxCapacity] and the ring's byte size fits.
    [[nodiscard]] static constexpr bool valid_capacity(std::size_t capacity) noexcept {
        return capacity >= 2 && std::has_single_bit(capacity) && capacity <= kMaxCapacity &&
               capacity <= (std::numeric_limits<std::size_t>::max() - kSlotsOffset) / sizeof(T);
    }

    /// Bytes needed for a ring of `capacity` slots. Precondition: valid_capacity(capacity).
    [[nodiscard]] static constexpr std::size_t required_bytes(std::size_t capacity) noexcept {
        return kSlotsOffset + (capacity * sizeof(T));
    }

    // The layout is ABI (ADR-004/ADR-009): any change here is a shared-memory format change.
    static_assert(sizeof(std::size_t) == 8, "fastbus targets 64-bit platforms only");
    static_assert(sizeof(std::atomic<IndexT>) == sizeof(IndexT), "atomic must be a bare counter");
    static_assert(std::is_standard_layout_v<Header>);
    static_assert(std::is_trivially_destructible_v<Header>);
    static_assert(alignof(Header) == kCacheLineSize);
    static_assert(sizeof(Header) == 4 * kFalseSharingRange);
    static_assert(offsetof(Header, tail) == 0 * kFalseSharingRange);
    static_assert(offsetof(Header, cached_head) == 1 * kFalseSharingRange);
    static_assert(offsetof(Header, head) == 2 * kFalseSharingRange);
    static_assert(offsetof(Header, cached_tail) == 3 * kFalseSharingRange);
    static_assert(kSlotsOffset % kCacheLineSize == 0 && kSlotsOffset % alignof(T) == 0);
};

}  // namespace fastbus::core
