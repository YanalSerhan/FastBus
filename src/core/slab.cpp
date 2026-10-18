// Slab block operations (M2.5): the block state machine. Layout and creation: slab_setup.cpp.
// Ordering tags [S1]-[S4]: docs/memory-ordering.md section 13.
#include "fastbus/core/slab.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

#include "fastbus/core/assert.hpp"
#include "fastbus/core/error.hpp"
#include "fastbus/core/result.hpp"

#include "slab_internal.hpp"

namespace fastbus::core {

using slab_detail::generation_of;
using slab_detail::kFree;
using slab_detail::kLoaned;
using slab_detail::kReturned;
using slab_detail::low_of;
using slab_detail::pack;

BlockHeader* SlabView::block(std::uint32_t index) const noexcept {
    return reinterpret_cast<BlockHeader*>(blocks_ +
                                          (static_cast<std::size_t>(index) * block_stride_));
}

std::uint32_t SlabView::free_count() const noexcept {
    return publisher_ ? header_->free_top : 0;
}

void SlabView::push_free(std::uint32_t index) noexcept {
    free_stack_[header_->free_top] = index;  // caller checked free_top < block_count_
    ++header_->free_top;
}

Result<BlockHandle> SlabView::allocate() noexcept {
    check(publisher_, "SlabView::allocate() on a subscriber view");
    const std::uint32_t top = header_->free_top;
    if (top == 0) {
        return fail(ErrorCode::PoolExhausted, "slab: no free block");
    }
    const std::uint32_t index = top <= block_count_ ? free_stack_[top - 1] : block_count_;
    if (index >= block_count_) {
        return fail(ErrorCode::Corrupted, "slab: free stack holds an invalid index");
    }
    BlockHeader* b = block(index);
    const std::uint64_t s = b->state.load(std::memory_order_relaxed);  // [S1]
    if (low_of(s) != kFree) {
        return fail(ErrorCode::Corrupted, "slab: free stack holds a block that is not free");
    }
    header_->free_top = top - 1;
    b->state.store(pack(generation_of(s), kLoaned), std::memory_order_relaxed);
    return BlockHandle{.index = index, .generation = generation_of(s)};
}

bool SlabView::share(BlockHandle handle, std::uint32_t refs) noexcept {
    if (!publisher_ || handle.index >= block_count_ || refs == 0 || refs > kMaxRefs) {
        return false;
    }
    BlockHeader* b = block(handle.index);
    if (b->state.load(std::memory_order_relaxed) != pack(handle.generation, kLoaned)) {
        return false;
    }
    // [S2] relaxed: the descriptor push ([P3], release) that follows publishes it.
    b->state.store(pack(handle.generation, refs), std::memory_order_relaxed);
    return true;
}

bool SlabView::free(BlockHandle handle) noexcept {
    if (!publisher_ || handle.index >= block_count_ || header_->free_top >= block_count_) {
        return false;
    }
    BlockHeader* b = block(handle.index);
    if (b->state.load(std::memory_order_relaxed) != pack(handle.generation, kLoaned)) {
        return false;
    }
    b->state.store(pack(handle.generation + 1, kFree), std::memory_order_relaxed);  // [S4]
    push_free(handle.index);
    return true;
}

bool SlabView::reclaim(std::uint32_t index) noexcept {
    if (!publisher_ || index >= block_count_ || header_->free_top >= block_count_) {
        return false;
    }
    BlockHeader* b = block(index);
    const std::uint64_t s = b->state.load(std::memory_order_acquire);
    if (low_of(s) != kReturned) {
        return false;  // FREE (double reclaim) or LOANED/SHARED (stolen): refuse
    }
    b->state.store(pack(generation_of(s) + 1, kFree), std::memory_order_relaxed);  // [S4]
    push_free(index);
    return true;
}

ReleaseResult SlabView::release(BlockHandle handle) noexcept {
    if (handle.index >= block_count_) {
        return ReleaseResult::Stale;
    }
    std::atomic<std::uint64_t>& state = block(handle.index)->state;
    std::uint64_t s = state.load(std::memory_order_relaxed);
    while (true) {
        const std::uint32_t refs = low_of(s);
        if (generation_of(s) != handle.generation || refs == kReturned || refs >= kLoaned) {
            return ReleaseResult::Stale;
        }
        // [S3] generation and count are checked and changed in one atomic step.
        if (state.compare_exchange_weak(s, s - 1, std::memory_order_acq_rel,
                                        std::memory_order_relaxed)) {
            return refs == 1 ? ReleaseResult::Last : ReleaseResult::Remaining;
        }
    }
}

std::span<std::byte> SlabView::payload(BlockHandle handle) const noexcept {
    if (handle.index >= block_count_) {
        return {};
    }
    BlockHeader* b = block(handle.index);
    const std::uint64_t s = b->state.load(std::memory_order_relaxed);
    if (generation_of(s) != handle.generation || low_of(s) == kFree) {
        return {};
    }
    return {reinterpret_cast<std::byte*>(b) + sizeof(BlockHeader), payload_capacity_};
}

}  // namespace fastbus::core
