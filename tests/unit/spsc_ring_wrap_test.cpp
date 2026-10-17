// Counter wrap-around tests (M1.6): small index types make the 8/16-bit counters overflow many
// times in a few thousand operations, exercising memory-ordering.md section 9 cheaply.
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

#include "fastbus/core/spsc_ring.hpp"

namespace {

using fastbus::core::SpscRing;

template <typename IndexT>
class SpscRingWrap : public ::testing::Test {
protected:
    static constexpr IndexT kMax = std::numeric_limits<IndexT>::max();

    static SpscRing<std::uint32_t, IndexT> make(std::size_t capacity, IndexT start) {
        auto ring = SpscRing<std::uint32_t, IndexT>::create(capacity, start);
        EXPECT_TRUE(ring.has_value());
        return std::move(ring).value();
    }
};

using IndexTypes = ::testing::Types<std::uint8_t, std::uint16_t>;
TYPED_TEST_SUITE(SpscRingWrap, IndexTypes);

TYPED_TEST(SpscRingWrap, FifoAcrossManyCounterWraps) {
    // Start two steps before the wrap; 3 * 2^bits items wrap the counters at least three times.
    const std::size_t items = 3U * (std::size_t{TestFixture::kMax} + 1U);
    for (const std::size_t capacity : {2U, 4U, 64U}) {
        auto ring = TestFixture::make(capacity, static_cast<TypeParam>(TestFixture::kMax - 1));
        std::uint32_t next_in = 0;
        std::uint32_t next_out = 0;
        while (next_out < items) {
            while (next_in < items && ring.try_push(next_in)) {
                ++next_in;
            }
            ASSERT_LE(ring.size_approx(), capacity);
            std::uint32_t out = 0;
            while (ring.try_pop(out)) {
                ASSERT_EQ(out, next_out++) << "capacity " << capacity;
            }
        }
        EXPECT_TRUE(ring.empty_approx());
    }
}

TYPED_TEST(SpscRingWrap, FullAtMaxCapacityAcrossTheWrapPoint) {
    // Max capacity = half the index range. Starting near the wrap means the full ring has
    // tail < head numerically; full and empty must still be told apart.
    constexpr std::size_t kCapacity = (std::size_t{TestFixture::kMax} + 1U) / 2U;
    auto ring = TestFixture::make(kCapacity, static_cast<TypeParam>(TestFixture::kMax - 9));
    for (std::uint32_t i = 0; i < kCapacity; ++i) {
        ASSERT_TRUE(ring.try_push(i)) << i;
    }
    EXPECT_EQ(ring.size_approx(), kCapacity);
    EXPECT_FALSE(ring.try_push(0));
    EXPECT_EQ(ring.try_claim(), nullptr);
    for (std::uint32_t i = 0; i < kCapacity; ++i) {
        std::uint32_t out = 0;
        ASSERT_TRUE(ring.try_pop(out));
        ASSERT_EQ(out, i);
    }
    EXPECT_EQ(ring.try_peek(), nullptr);
    EXPECT_TRUE(ring.empty_approx());
}

TYPED_TEST(SpscRingWrap, ClaimAndPeekWorkOnTheWrappingSlot) {
    auto ring = TestFixture::make(4, TestFixture::kMax);  // the next counter is the maximum
    auto* slot = ring.try_claim();
    ASSERT_NE(slot, nullptr);
    *slot = 77;
    ring.commit();  // counter wraps from max to 0 here
    const auto* peeked = ring.try_peek();
    ASSERT_NE(peeked, nullptr);
    EXPECT_EQ(*peeked, 77U);
    ring.release();
    EXPECT_TRUE(ring.empty_approx());
}

}  // namespace
