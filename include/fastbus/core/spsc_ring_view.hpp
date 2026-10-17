/// @file spsc_ring_view.hpp
/// @brief Lock-free, wait-free single-producer/single-consumer ring (M1.5, PRD_spsc_ring.md).
///
/// `SpscRingView` works on caller-provided memory laid out by `SpscRingLayout`; it never
/// allocates. Each atomic operation carries the tag of its row in docs/memory-ordering.md §4,
/// and the order used there is the only one allowed here.
///
/// Threads: producer functions (try_push, try_claim, commit) from one thread, consumer functions
/// (try_pop, try_peek, release) from one thread (may be the same). Debug builds check this.
#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>

#include "fastbus/core/assert.hpp"
#include "fastbus/core/result.hpp"
#include "fastbus/core/spsc_ring_layout.hpp"
#include "fastbus/core/thread_owner.hpp"

namespace fastbus::core {

template <RingElement T, RingIndex IndexT = std::uint64_t>
class SpscRingView {
public:
    using Layout = SpscRingLayout<T, IndexT>;
    using Header = typename Layout::Header;

    /// Bytes and alignment `create` needs for `capacity` slots (capacity must be valid).
    [[nodiscard]] static constexpr std::size_t required_bytes(std::size_t capacity) noexcept {
        return Layout::required_bytes(capacity);
    }
    [[nodiscard]] static constexpr std::size_t required_alignment() noexcept {
        return Layout::kRequiredAlignment;
    }

    /// Initializes an empty ring in `memory`. `start` sets both counters (tests use it to start
    /// near the counter wrap point). Errors: InvalidCapacity, BufferTooSmall, Misaligned.
    [[nodiscard]] static Result<SpscRingView> create(std::span<std::byte> memory,
                                                     std::size_t capacity,
                                                     IndexT start = 0) noexcept {
        if (!Layout::valid_capacity(capacity)) {
            return fail(ErrorCode::InvalidCapacity, "capacity must be a power of two in [2, max]");
        }
        if (memory.size() < Layout::required_bytes(capacity)) {
            return fail(ErrorCode::BufferTooSmall, "memory smaller than required_bytes()");
        }
        if (reinterpret_cast<std::uintptr_t>(memory.data()) % Layout::kRequiredAlignment != 0) {
            return fail(ErrorCode::Misaligned, "memory not aligned to required_alignment()");
        }
        auto* header = std::construct_at(reinterpret_cast<Header*>(memory.data()),
                                         static_cast<IndexT>(capacity - 1), start);
        auto* first = reinterpret_cast<T*>(memory.data() + Layout::kSlotsOffset);
        return SpscRingView{header, std::span<T>{first, capacity}};
    }

    // ---- Producer --------------------------------------------------------------------------
    /// Copies `value` into the ring. Returns false (ring unchanged) if it is full.
    [[nodiscard]] bool try_push(const T& value) noexcept {
        T* slot = try_claim();
        if (slot == nullptr) {
            return false;
        }
        std::memcpy(static_cast<void*>(slot), &value, sizeof(T));
        commit();
        return true;
    }

    /// Returns the next free slot for in-place writing, or nullptr if full. Nothing is published
    /// until commit(); calling again before commit() returns the same slot.
    [[nodiscard]] T* try_claim() noexcept {
        producer_.check("SpscRingView: producer used from two threads");
        Header& h = *header_;
        const IndexT tail = h.tail.load(std::memory_order_relaxed);  // [P1]
        // Occupancy is in [0, capacity]; "> mask" means "== capacity", i.e. looks full.
        if (ring_distance(h.cached_head, tail) > h.producer_mask) {
            h.cached_head = h.head.load(std::memory_order_acquire);  // [P2] pairs with [C3]
            if (ring_distance(h.cached_head, tail) > h.producer_mask) {
                return nullptr;
            }
        }
        if constexpr (kDebugChecks) {
            claimed_ = true;
        }
        return &slots_[ring_slot(tail, h.producer_mask)];
    }

    /// Publishes the slot returned by try_claim(). Precondition: a claim is outstanding.
    void commit() noexcept {
        if constexpr (kDebugChecks) {
            producer_.check("SpscRingView: producer used from two threads");
            check(claimed_, "SpscRingView::commit() without try_claim()");
            claimed_ = false;
        }
        Header& h = *header_;
        const IndexT tail = h.tail.load(std::memory_order_relaxed);  // [P1]
        h.tail.store(ring_next(tail), std::memory_order_release);    // [P3] pairs with [C2]
    }

    // ---- Consumer --------------------------------------------------------------------------
    /// Moves the oldest item into `out`. Returns false (out untouched) if the ring is empty.
    [[nodiscard]] bool try_pop(T& out) noexcept {
        const T* slot = try_peek();
        if (slot == nullptr) {
            return false;
        }
        std::memcpy(static_cast<void*>(&out), slot, sizeof(T));
        release();
        return true;
    }

    /// Returns the oldest item in place, or nullptr if empty. The slot stays owned by the
    /// consumer until release(); calling again before release() returns the same slot.
    [[nodiscard]] const T* try_peek() noexcept {
        consumer_.check("SpscRingView: consumer used from two threads");
        Header& h = *header_;
        const IndexT head = h.head.load(std::memory_order_relaxed);  // [C1]
        if (h.cached_tail == head) {                                 // looks empty
            h.cached_tail = h.tail.load(std::memory_order_acquire);  // [C2] pairs with [P3]
            if (h.cached_tail == head) {
                return nullptr;
            }
        }
        if constexpr (kDebugChecks) {
            peeked_ = true;
        }
        return &slots_[ring_slot(head, h.consumer_mask)];
    }

    /// Frees the slot returned by try_peek(). Precondition: a peek is outstanding.
    void release() noexcept {
        if constexpr (kDebugChecks) {
            consumer_.check("SpscRingView: consumer used from two threads");
            check(peeked_, "SpscRingView::release() without try_peek()");
            peeked_ = false;
        }
        Header& h = *header_;
        const IndexT head = h.head.load(std::memory_order_relaxed);  // [C1]
        h.head.store(ring_next(head), std::memory_order_release);    // [C3] pairs with [P2]
    }

    // ---- Any thread --------------------------------------------------------------------------
    /// Items in the ring: exact when both sides are idle, within [0, capacity] always.
    [[nodiscard]] std::size_t size_approx() const noexcept {
        const IndexT head = header_->head.load(std::memory_order_acquire);  // [Q1]
        const IndexT tail = header_->tail.load(std::memory_order_relaxed);  // [Q2]
        return std::min<std::size_t>(ring_distance(head, tail), slots_.size());
    }
    [[nodiscard]] bool empty_approx() const noexcept { return size_approx() == 0; }
    [[nodiscard]] std::size_t capacity() const noexcept { return slots_.size(); }

private:
    SpscRingView(Header* header, std::span<T> slots) noexcept : header_(header), slots_(slots) {}

    // Read-only after create(): shared by both threads without false sharing in release builds.
    Header* header_;
    std::span<T> slots_;
    // Debug-only state (never written when kDebugChecks is false).
    ThreadOwner producer_;
    ThreadOwner consumer_;
    bool claimed_ = false;  // producer-only
    bool peeked_ = false;   // consumer-only
};

}  // namespace fastbus::core
