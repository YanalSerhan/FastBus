#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>

#include "fastbus/core/cache_line.hpp"
#include "fastbus/core/spsc_ring_layout.hpp"

namespace {

using fastbus::core::kCacheLineSize;
using fastbus::core::kFalseSharingRange;
using fastbus::core::ring_distance;
using fastbus::core::ring_next;
using fastbus::core::ring_slot;
using fastbus::core::RingElement;
using fastbus::core::RingIndex;
using fastbus::core::SpscRingHeader;
using fastbus::core::SpscRingLayout;

struct Payload64 {
    std::array<std::uint64_t, 8> words;
};
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)  // padded due to alignas: intended here
#endif
struct alignas(128) OverAligned {
    int x;
};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

// --- Concepts (compile-time) ---------------------------------------------------------------------
static_assert(RingElement<std::uint64_t>);
static_assert(RingElement<Payload64>);
static_assert(!RingElement<std::string>);
static_assert(!RingElement<std::unique_ptr<int>>);  // not trivially copyable
static_assert(!RingElement<OverAligned>);
static_assert(!RingElement<const int>);
static_assert(RingIndex<std::uint8_t> && RingIndex<std::uint16_t> && RingIndex<std::uint32_t> &&
              RingIndex<std::uint64_t>);
static_assert(!RingIndex<bool> && !RingIndex<int> && !RingIndex<std::int64_t>);

// --- Layout (every IndexT instantiates the static_asserts in SpscRingLayout) --------------------
static_assert(SpscRingLayout<std::uint8_t, std::uint8_t>::kSlotsOffset == 512);
static_assert(SpscRingLayout<Payload64, std::uint16_t>::kSlotsOffset == 512);
static_assert(SpscRingLayout<std::uint32_t, std::uint32_t>::kSlotsOffset == 512);
static_assert(SpscRingLayout<std::uint64_t, std::uint64_t>::kSlotsOffset == 512);
static_assert(SpscRingLayout<std::uint64_t, std::uint64_t>::kRequiredAlignment == kCacheLineSize);

// --- Capacity rule (memory-ordering.md section 9)
// -------------------------------------------------
TEST(SpscRingLayout, MaxCapacityIsHalfTheIndexRange) {
    EXPECT_EQ((SpscRingLayout<int, std::uint8_t>::kMaxCapacity), 128U);
    EXPECT_EQ((SpscRingLayout<int, std::uint16_t>::kMaxCapacity), 32'768U);
    EXPECT_EQ((SpscRingLayout<int, std::uint32_t>::kMaxCapacity), std::size_t{1} << 31U);
    EXPECT_EQ((SpscRingLayout<char, std::uint64_t>::kMaxCapacity), std::size_t{1} << 63U);
}

TEST(SpscRingLayout, ValidCapacityAcceptsPowersOfTwoInRange) {
    using L8 = SpscRingLayout<int, std::uint8_t>;
    for (const std::size_t c : {2U, 4U, 64U, 128U}) {
        EXPECT_TRUE(L8::valid_capacity(c)) << c;
    }
    for (const std::size_t c : {0U, 1U, 3U, 6U, 100U, 256U, 512U}) {
        EXPECT_FALSE(L8::valid_capacity(c)) << c;
    }
}

TEST(SpscRingLayout, ValidCapacityRejectsByteSizeOverflow) {
    using L = SpscRingLayout<Payload64, std::uint64_t>;  // 64-byte slots
    EXPECT_TRUE(L::valid_capacity(std::size_t{1} << 40U));
    EXPECT_FALSE(L::valid_capacity(std::size_t{1} << 58U));  // 2^58 * 64 overflows size_t
}

TEST(SpscRingLayout, RequiredBytesIsHeaderPlusSlots) {
    EXPECT_EQ((SpscRingLayout<std::uint64_t, std::uint64_t>::required_bytes(2)), 512U + 16U);
    EXPECT_EQ((SpscRingLayout<Payload64, std::uint16_t>::required_bytes(4096)), 512U + 262'144U);
}

// --- Header -----------------------------------------------------------------------------------
TEST(SpscRingHeader, ConstructsEmptyWithMaskOnBothSides) {
    const auto header = std::make_unique<SpscRingHeader<std::uint16_t>>(std::uint16_t{63});
    EXPECT_EQ(header->tail.load(), 0U);
    EXPECT_EQ(header->head.load(), 0U);
    EXPECT_EQ(header->cached_head, 0U);
    EXPECT_EQ(header->cached_tail, 0U);
    EXPECT_EQ(header->producer_mask, 63U);
    EXPECT_EQ(header->consumer_mask, 63U);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(header.get()) % kCacheLineSize, 0U);
}

TEST(SpscRingHeader, SidesLiveInSeparateFalseSharingBlocks) {
    const auto header = std::make_unique<SpscRingHeader<std::uint64_t>>(std::uint64_t{7});
    const auto block = [&](const void* field) {
        return (reinterpret_cast<std::uintptr_t>(field) -
                reinterpret_cast<std::uintptr_t>(header.get())) /
               kFalseSharingRange;
    };
    EXPECT_EQ(block(&header->tail), 0U);
    EXPECT_EQ(block(&header->cached_head), 1U);
    EXPECT_EQ(block(&header->producer_mask), 1U);
    EXPECT_EQ(block(&header->head), 2U);
    EXPECT_EQ(block(&header->cached_tail), 3U);
    EXPECT_EQ(block(&header->consumer_mask), 3U);
}

// --- Counter arithmetic under wrap ------------------------------------------------------------
TEST(RingArithmetic, DistanceIsModularForSmallIndexTypes) {
    EXPECT_EQ(ring_distance<std::uint8_t>(250, 2), 8U);  // 250 -> 255 -> 0 -> 2
    EXPECT_EQ(ring_distance<std::uint8_t>(7, 7), 0U);
    EXPECT_EQ(ring_distance<std::uint8_t>(0, 128), 128U);  // a full ring at max capacity
    EXPECT_EQ(ring_distance<std::uint16_t>(65'530, 10), 16U);
    EXPECT_EQ(ring_distance<std::uint64_t>(std::numeric_limits<std::uint64_t>::max(), 0), 1U);
}

TEST(RingArithmetic, NextWrapsToZero) {
    EXPECT_EQ(ring_next<std::uint8_t>(255), 0U);
    EXPECT_EQ(ring_next<std::uint16_t>(65'535), 0U);
    EXPECT_EQ(ring_next<std::uint8_t>(41), 42U);
}

TEST(RingArithmetic, SlotMasksTheCounter) {
    EXPECT_EQ(ring_slot<std::uint8_t>(255, 63), 63U);
    EXPECT_EQ(ring_slot<std::uint8_t>(ring_next<std::uint8_t>(255), 63), 0U);
    EXPECT_EQ(ring_slot<std::uint64_t>(4'100, 4'095), 4U);
}

}  // namespace
