// OffsetPtr tests (M2.3): offsets read from shared memory are untrusted; resolve() must reject
// every out-of-bounds, overflowing or misaligned value.
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

#include "fastbus/core/offset_ptr.hpp"

namespace {

using fastbus::core::offset_of;
using fastbus::core::OffsetPtr;
using fastbus::core::resolve;
using fastbus::core::resolve_array;

struct Pair {
    std::uint64_t a;
    std::uint64_t b;
};

class OffsetPtrTest : public ::testing::Test {
protected:
    [[nodiscard]] std::span<std::byte> base() { return storage_; }
    [[nodiscard]] std::byte* start() { return storage_.data(); }

private:
    alignas(64) std::array<std::byte, 256> storage_{};
};

TEST_F(OffsetPtrTest, ResolvesInBoundsAlignedOffsets) {
    Pair* p = resolve(OffsetPtr<Pair>{.offset = 64}, base());
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(reinterpret_cast<std::byte*>(p), start() + 64);
    EXPECT_NE(resolve(OffsetPtr<Pair>{.offset = 256 - sizeof(Pair)}, base()), nullptr);  // last fit
}

TEST_F(OffsetPtrTest, NullOutOfBoundsAndMisalignedAreRejected) {
    EXPECT_EQ(resolve(OffsetPtr<Pair>{.offset = 0}, base()), nullptr);  // null
    EXPECT_EQ(resolve(OffsetPtr<Pair>{.offset = 256 - sizeof(Pair) + 8}, base()), nullptr);
    EXPECT_EQ(resolve(OffsetPtr<Pair>{.offset = 256}, base()), nullptr);
    EXPECT_EQ(resolve(OffsetPtr<Pair>{.offset = 4}, base()), nullptr);  // misaligned
    EXPECT_EQ(resolve(OffsetPtr<Pair>{.offset = std::numeric_limits<std::uint64_t>::max()}, base()),
              nullptr);
}

TEST_F(OffsetPtrTest, ArraysAreBoundsCheckedWithoutOverflow) {
    EXPECT_EQ(resolve_array(OffsetPtr<Pair>{.offset = 64}, 12, base()).size(), 12U);  // 64+192
    EXPECT_TRUE(resolve_array(OffsetPtr<Pair>{.offset = 64}, 13, base()).empty());
    EXPECT_TRUE(resolve_array(OffsetPtr<Pair>{.offset = 64}, 0, base()).empty());
    // count * sizeof(T) would overflow 64 bits and wrap to a small number: must be rejected.
    EXPECT_TRUE(resolve_array(OffsetPtr<Pair>{.offset = 64}, (std::uint64_t{1} << 60U) + 1, base())
                    .empty());
}

TEST_F(OffsetPtrTest, OffsetOfRoundTrips) {
    Pair* p = resolve(OffsetPtr<Pair>{.offset = 128}, base());
    ASSERT_NE(p, nullptr);
    const auto back = offset_of(p, std::span<const std::byte>{base()});
    EXPECT_EQ(back.offset, 128U);
    EXPECT_EQ(resolve(back, base()), p);
}

}  // namespace
