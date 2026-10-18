// Slab layout, creation and attach-time validation (M2.5). Block operations: slab.cpp.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <span>

#include "fastbus/core/error.hpp"
#include "fastbus/core/result.hpp"
#include "fastbus/core/slab.hpp"

#include "slab_internal.hpp"

namespace fastbus::core {
namespace {

using slab_detail::kFree;
using slab_detail::pack;

struct Geometry {
    std::size_t stride = 0;
    std::size_t free_stack_offset = 0;
    std::size_t blocks_offset = 0;
    std::size_t total = 0;
};

/// Layout for `count` blocks of `payload` bytes; false if invalid or any size overflows.
bool geometry(std::uint64_t count, std::uint64_t payload, Geometry& g) noexcept {
    constexpr std::size_t kMax = std::numeric_limits<std::size_t>::max();
    if (count == 0 || count > std::numeric_limits<std::uint32_t>::max() || payload == 0 ||
        payload > kMax - (2 * kCacheLineSize)) {
        return false;
    }
    const std::size_t rounded = (payload + kCacheLineSize - 1) / kCacheLineSize * kCacheLineSize;
    g.stride = sizeof(BlockHeader) + rounded;
    if (g.stride > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    g.free_stack_offset = sizeof(SlabHeader);
    const std::size_t stack_end = g.free_stack_offset + (count * sizeof(std::uint32_t));
    g.blocks_offset = (stack_end + kCacheLineSize - 1) / kCacheLineSize * kCacheLineSize;
    if (count > (kMax - g.blocks_offset) / g.stride) {
        return false;
    }
    g.total = g.blocks_offset + (count * g.stride);
    return true;
}

bool misaligned(const std::byte* p) noexcept {
    return reinterpret_cast<std::uintptr_t>(p) % kCacheLineSize != 0;
}

}  // namespace

std::size_t SlabView::required_bytes(std::uint32_t block_count,
                                     std::size_t payload_capacity) noexcept {
    Geometry g;
    return geometry(block_count, payload_capacity, g) ? g.total : 0;
}

Result<SlabView> SlabView::create(std::span<std::byte> memory, std::uint32_t block_count,
                                  std::size_t payload_capacity) noexcept {
    Geometry g;
    if (!geometry(block_count, payload_capacity, g)) {
        return fail(ErrorCode::InvalidArgument, "slab: block count or payload size out of range");
    }
    if (memory.size() < g.total) {
        return fail(ErrorCode::BufferTooSmall, "slab: memory smaller than required_bytes()");
    }
    if (misaligned(memory.data())) {
        return fail(ErrorCode::Misaligned, "slab: memory not 64-byte aligned");
    }
    SlabView view;
    view.header_ =
        std::construct_at(reinterpret_cast<SlabHeader*>(memory.data()),
                          SlabHeader{.magic = SlabHeader::kMagic,
                                     .version = SlabHeader::kVersion,
                                     .block_count = block_count,
                                     .block_stride = static_cast<std::uint32_t>(g.stride),
                                     .payload_capacity = payload_capacity,
                                     .free_stack_offset = g.free_stack_offset,
                                     .blocks_offset = g.blocks_offset,
                                     .free_top = block_count,
                                     .reserved = {}});
    view.free_stack_ = {reinterpret_cast<std::uint32_t*>(memory.data() + g.free_stack_offset),
                        block_count};
    view.blocks_ = memory.data() + g.blocks_offset;
    view.block_count_ = block_count;
    view.block_stride_ = static_cast<std::uint32_t>(g.stride);
    view.payload_capacity_ = payload_capacity;
    view.publisher_ = true;
    for (std::uint32_t i = 0; i < block_count; ++i) {
        view.free_stack_[i] = block_count - 1 - i;  // pop order 0, 1, 2, ...
        BlockHeader* b = std::construct_at(view.block(i));
        b->state.store(pack(0, kFree), std::memory_order_relaxed);
    }
    return view;
}

Result<SlabView> SlabView::attach(std::span<std::byte> memory) noexcept {
    if (memory.size() < sizeof(SlabHeader)) {
        return fail(ErrorCode::BufferTooSmall, "slab: memory smaller than the header");
    }
    if (misaligned(memory.data())) {
        return fail(ErrorCode::Misaligned, "slab: memory not 64-byte aligned");
    }
    SlabHeader copy{};  // validate a private copy: the shared one may change under us
    std::memcpy(&copy, memory.data(), sizeof(copy));
    if (copy.magic != SlabHeader::kMagic) {
        return fail(ErrorCode::Corrupted, "slab: bad magic");
    }
    if (copy.version != SlabHeader::kVersion) {
        return fail(ErrorCode::VersionMismatch, "slab: unknown version");
    }
    Geometry g;
    if (!geometry(copy.block_count, copy.payload_capacity, g) || g.stride != copy.block_stride ||
        g.free_stack_offset != copy.free_stack_offset || g.blocks_offset != copy.blocks_offset) {
        return fail(ErrorCode::Corrupted, "slab: inconsistent geometry");
    }
    if (memory.size() < g.total) {
        return fail(ErrorCode::BufferTooSmall, "slab: header describes more memory than mapped");
    }
    SlabView view;
    view.header_ = reinterpret_cast<SlabHeader*>(memory.data());
    view.blocks_ = memory.data() + g.blocks_offset;
    view.block_count_ = copy.block_count;
    view.block_stride_ = copy.block_stride;
    view.payload_capacity_ = static_cast<std::size_t>(copy.payload_capacity);
    view.publisher_ = false;  // no free stack access: subscribers never allocate
    return view;
}

}  // namespace fastbus::core
