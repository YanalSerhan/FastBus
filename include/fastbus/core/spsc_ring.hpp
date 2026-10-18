/// @file spsc_ring.hpp
/// @brief Owning SPSC ring (M1.5): `SpscRing` allocates one aligned block in create() and puts a
/// `SpscRingView` on it. Include this header for the whole ring API; the protocol is documented
/// in spsc_ring_view.hpp and docs/memory-ordering.md.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <span>
#include <utility>

#include "fastbus/core/result.hpp"
#include "fastbus/core/spsc_ring_layout.hpp"
#include "fastbus/core/spsc_ring_view.hpp"

namespace fastbus::core {

/// Owning ring: one aligned allocation in create(), none afterwards. Move-only.
template <RingElement T, RingIndex IndexT = std::uint64_t>
class SpscRing : public SpscRingView<T, IndexT> {
public:
    using View = SpscRingView<T, IndexT>;

    /// Errors: InvalidCapacity, OutOfMemory.
    [[nodiscard]] static Result<SpscRing> create(std::size_t capacity, IndexT start = 0) noexcept {
        if (!View::Layout::valid_capacity(capacity)) {
            return fail(ErrorCode::InvalidCapacity, "capacity must be a power of two in [2, max]");
        }
        const std::size_t bytes = View::required_bytes(capacity);
        Memory memory{static_cast<std::byte*>(
            ::operator new(bytes, std::align_val_t{View::required_alignment()}, std::nothrow))};
        if (!memory) {
            return fail(ErrorCode::OutOfMemory, "SpscRing allocation failed");
        }
        Result<View> view = View::create(std::span{memory.get(), bytes}, capacity, start);
        if (!view) {
            return Unexpected{view.error()};  // unreachable: size and alignment are ours
        }
        return SpscRing{std::move(view).value(), std::move(memory)};
    }

    /// The non-owning view (e.g. to hand to the producer and consumer threads).
    [[nodiscard]] View& view() noexcept { return *this; }

private:
    struct AlignedDelete {
        void operator()(std::byte* p) const noexcept {
            ::operator delete(p, std::align_val_t{View::required_alignment()});
        }
    };
    using Memory = std::unique_ptr<std::byte, AlignedDelete>;

    SpscRing(const View& view, Memory memory) noexcept : View(view), memory_(std::move(memory)) {}

    Memory memory_;
};

}  // namespace fastbus::core
