// SlabView unit tests (M2.5): layout, the block state machine, exhaustion, stale handles,
// double release / double reclaim, and attach() on corrupted headers.
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <set>
#include <span>
#include <utility>
#include <vector>

#include "fastbus/core/error.hpp"
#include "fastbus/core/slab.hpp"

namespace {

using fastbus::core::BlockHandle;
using fastbus::core::ErrorCode;
using fastbus::core::ReleaseResult;
using fastbus::core::SlabHeader;
using fastbus::core::SlabView;

template <typename R>
ErrorCode error_of(const R& r) {
    return r.has_value() ? ErrorCode{0} : r.error().code;
}

/// 64-byte aligned, zeroed test memory.
class Memory {
public:
    explicit Memory(std::size_t size)
        : size_(size), data_(static_cast<std::byte*>(::operator new(size, std::align_val_t{64}))) {
        std::memset(data_.get(), 0, size);
    }
    [[nodiscard]] std::span<std::byte> span() const { return {data_.get(), size_}; }

private:
    struct Delete {
        void operator()(std::byte* p) const { ::operator delete(p, std::align_val_t{64}); }
    };
    std::size_t size_;
    std::unique_ptr<std::byte, Delete> data_;
};

struct Fixture {
    explicit Fixture(std::uint32_t blocks, std::size_t payload = 100)
        : memory(SlabView::required_bytes(blocks, payload)),
          slab(SlabView::create(memory.span(), blocks, payload).value()) {}
    Memory memory;
    SlabView slab;
};

TEST(Slab, RequiredBytesRejectsInvalidAndOverflowingSizes) {
    EXPECT_EQ(SlabView::required_bytes(0, 64), 0U);
    EXPECT_EQ(SlabView::required_bytes(4, 0), 0U);
    EXPECT_EQ(SlabView::required_bytes(0xFFFF'FFFFU, std::size_t{1} << 40U), 0U);  // overflow
    // header 64 + stack 4*4 rounded to 64 + 4 blocks * (64 + 128)
    EXPECT_EQ(SlabView::required_bytes(4, 100), 64U + 64U + (4U * 192U));
}

TEST(Slab, CreateRejectsBadMemory) {
    const Memory m(4096);
    EXPECT_EQ(error_of(SlabView::create(m.span().first(100), 4, 100)), ErrorCode::BufferTooSmall);
    EXPECT_EQ(error_of(SlabView::create(m.span().subspan(8), 4, 100)), ErrorCode::Misaligned);
    EXPECT_EQ(error_of(SlabView::create(m.span(), 0, 100)), ErrorCode::InvalidArgument);
}

TEST(Slab, PayloadsAreAlignedDistinctAndOfTheRightSize) {
    Fixture f(8);
    std::set<std::byte*> seen;
    for (int i = 0; i < 8; ++i) {
        const BlockHandle h = f.slab.allocate().value();
        const auto p = f.slab.payload(h);
        ASSERT_EQ(p.size(), 100U);
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p.data()) % 64U, 0U);
        EXPECT_TRUE(seen.insert(p.data()).second);
        std::memset(p.data(), i, p.size());  // must not clobber any other block (ASan checks)
    }
}

TEST(Slab, ExhaustionIsAnErrorNotAWait) {
    Fixture f(2);
    ASSERT_TRUE(f.slab.allocate().has_value());
    ASSERT_TRUE(f.slab.allocate().has_value());
    EXPECT_EQ(f.slab.free_count(), 0U);
    EXPECT_EQ(error_of(f.slab.allocate()), ErrorCode::PoolExhausted);
}

TEST(Slab, FullLifecycleWithThreeSubscribers) {
    Fixture f(4);
    const BlockHandle h = f.slab.allocate().value();
    ASSERT_TRUE(f.slab.share(h, 3));
    EXPECT_EQ(f.slab.release(h), ReleaseResult::Remaining);
    EXPECT_EQ(f.slab.release(h), ReleaseResult::Remaining);
    EXPECT_EQ(f.slab.release(h), ReleaseResult::Last);
    EXPECT_EQ(f.slab.free_count(), 3U);  // not free until the publisher reclaims it
    ASSERT_TRUE(f.slab.reclaim(h.index));
    EXPECT_EQ(f.slab.free_count(), 4U);

    // The next life of the same block has a new generation.
    BlockHandle again{};
    do {
        again = f.slab.allocate().value();
    } while (again.index != h.index);
    EXPECT_NE(again.generation, h.generation);
}

TEST(Slab, StaleHandlesChangeNothing) {
    Fixture f(1);
    const BlockHandle old = f.slab.allocate().value();
    ASSERT_TRUE(f.slab.share(old, 1));
    ASSERT_EQ(f.slab.release(old), ReleaseResult::Last);
    ASSERT_TRUE(f.slab.reclaim(old.index));
    const BlockHandle now = f.slab.allocate().value();  // same index, next generation
    ASSERT_EQ(now.index, old.index);
    ASSERT_TRUE(f.slab.share(now, 2));

    EXPECT_EQ(f.slab.release(old), ReleaseResult::Stale);  // must not touch the new message
    EXPECT_TRUE(f.slab.payload(old).empty());
    EXPECT_EQ(f.slab.release(now), ReleaseResult::Remaining);  // still 2 -> 1, untouched by old
    EXPECT_EQ(f.slab.release(now), ReleaseResult::Last);
}

TEST(Slab, DoubleReleaseAndDoubleReclaimAreRefused) {
    // Pool larger than the blocks in use, so the free stack has room and only the block-state
    // check can refuse the second reclaim.
    Fixture f(4);
    const BlockHandle h = f.slab.allocate().value();
    const BlockHandle other = f.slab.allocate().value();  // keeps the free stack below full
    ASSERT_TRUE(f.slab.share(h, 1));
    ASSERT_EQ(f.slab.release(h), ReleaseResult::Last);
    EXPECT_EQ(f.slab.release(h), ReleaseResult::Stale);  // second release: no underflow
    ASSERT_TRUE(f.slab.reclaim(h.index));
    EXPECT_FALSE(f.slab.reclaim(h.index));  // would put the index on the free stack twice
    EXPECT_EQ(f.slab.free_count(), 3U);
    EXPECT_TRUE(f.slab.free(other));
}

TEST(Slab, LoansCannotBeStolenOrReleased) {
    Fixture f(2);
    const BlockHandle loan = f.slab.allocate().value();
    EXPECT_FALSE(f.slab.reclaim(loan.index));                  // LOANED is not RETURNED
    EXPECT_EQ(f.slab.release(loan), ReleaseResult::Stale);     // nothing to release yet
    EXPECT_FALSE(f.slab.share(loan, 0));                       // zero subscribers is invalid
    EXPECT_FALSE(f.slab.share(loan, SlabView::kMaxRefs + 1));  // would collide with LOANED
    ASSERT_TRUE(f.slab.free(loan));                            // loan dropped without publish
    EXPECT_FALSE(f.slab.free(loan));                           // and only once
    EXPECT_FALSE(f.slab.share(loan, 1));                       // the old loan is gone
    EXPECT_EQ(f.slab.free_count(), 2U);
}

TEST(Slab, OutOfRangeIndicesAreHarmless) {
    Fixture f(2);
    const BlockHandle bogus{.index = 99, .generation = 0};
    EXPECT_EQ(f.slab.release(bogus), ReleaseResult::Stale);
    EXPECT_TRUE(f.slab.payload(bogus).empty());
    EXPECT_FALSE(f.slab.reclaim(99));
    EXPECT_FALSE(f.slab.share(bogus, 1));
}

TEST(Slab, AttachSeesThePublishersBlocks) {
    Fixture f(4);
    const BlockHandle h = f.slab.allocate().value();
    f.slab.payload(h)[0] = std::byte{0x7E};
    ASSERT_TRUE(f.slab.share(h, 1));

    auto sub = SlabView::attach(f.memory.span());
    ASSERT_TRUE(sub.has_value());
    EXPECT_EQ(sub->capacity(), 4U);
    EXPECT_EQ(sub->payload(h)[0], std::byte{0x7E});
    EXPECT_EQ(sub->release(h), ReleaseResult::Last);
    EXPECT_TRUE(f.slab.reclaim(h.index));
}

TEST(Slab, AttachRejectsCorruptedHeadersWithoutCrashing) {
    const Fixture f(4);
    const auto memory = f.memory.span();
    EXPECT_EQ(error_of(SlabView::attach(memory.first(32))), ErrorCode::BufferTooSmall);
    EXPECT_EQ(error_of(SlabView::attach(memory.first(SlabView::required_bytes(4, 100) - 1))),
              ErrorCode::BufferTooSmall);

    // Flip every bit of the header in turn: attach must either succeed with sane values or fail
    // cleanly. ASan/UBSan runs prove there is no out-of-bounds access either way.
    for (std::size_t bit = 0; bit < sizeof(SlabHeader) * 8; ++bit) {
        memory[bit / 8] ^= std::byte{static_cast<std::uint8_t>(1U << (bit % 8))};
        const auto r = SlabView::attach(memory);
        if (r.has_value()) {
            EXPECT_LE(SlabView::required_bytes(r->capacity(), r->payload_capacity()),
                      memory.size());
        }
        memory[bit / 8] ^= std::byte{static_cast<std::uint8_t>(1U << (bit % 8))};
    }
    EXPECT_TRUE(SlabView::attach(memory).has_value());  // restored
}

}  // namespace
