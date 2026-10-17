// Unit and edge-case tests for SpscRingView / SpscRing (M1.6, PRD_spsc_ring.md section 7).
#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <thread>
#include <utility>

#include "fastbus/core/assert.hpp"
#include "fastbus/core/error.hpp"
#include "fastbus/core/spsc_ring.hpp"

namespace {

using fastbus::core::ErrorCode;
using fastbus::core::kDebugChecks;
using fastbus::core::SpscRing;
using fastbus::core::SpscRingView;

using Ring = SpscRing<std::uint64_t>;

Ring make_ring(std::size_t capacity) {
    auto ring = Ring::create(capacity);
    EXPECT_TRUE(ring.has_value());
    return std::move(ring).value();
}

// ---- create ----------------------------------------------------------------------------------
TEST(SpscRingCreate, RejectsInvalidCapacities) {
    for (const std::size_t c : {0U, 1U, 3U, 6U, 1000U}) {
        const auto ring = Ring::create(c);
        ASSERT_FALSE(ring.has_value()) << c;
        EXPECT_EQ(ring.error().code, ErrorCode::InvalidCapacity) << c;
    }
    EXPECT_EQ((SpscRing<int, std::uint8_t>::create(256).error().code), ErrorCode::InvalidCapacity);
}

TEST(SpscRingCreate, ViewRejectsSmallAndMisalignedMemory) {
    using View = SpscRingView<std::uint64_t>;
    alignas(64) std::array<std::byte, View::required_bytes(4) + 64> buffer{};
    const std::span<std::byte> all{buffer};

    EXPECT_EQ(View::create(all.first(View::required_bytes(4) - 1), 4).error().code,
              ErrorCode::BufferTooSmall);
    EXPECT_EQ(View::create(all.subspan(8), 4).error().code, ErrorCode::Misaligned);
    EXPECT_EQ(View::create(all, 3).error().code, ErrorCode::InvalidCapacity);

    auto view = View::create(all, 4);
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(view->capacity(), 4U);
    EXPECT_TRUE(view->empty_approx());
}

// ---- empty / full ------------------------------------------------------------------------------
TEST(SpscRing, EmptyRingRefusesPopAndPeek) {
    auto ring = make_ring(4);
    std::uint64_t out = 42;
    EXPECT_FALSE(ring.try_pop(out));
    EXPECT_EQ(out, 42U);  // untouched
    EXPECT_EQ(ring.try_peek(), nullptr);
    EXPECT_EQ(ring.size_approx(), 0U);
}

TEST(SpscRing, FullRingRefusesPushAndClaimAndStaysUnchanged) {
    auto ring = make_ring(4);
    for (std::uint64_t i = 0; i < 4; ++i) {
        ASSERT_TRUE(ring.try_push(i));
    }
    EXPECT_EQ(ring.size_approx(), 4U);
    EXPECT_FALSE(ring.try_push(99));
    EXPECT_EQ(ring.try_claim(), nullptr);
    for (std::uint64_t i = 0; i < 4; ++i) {
        std::uint64_t out = 0;
        ASSERT_TRUE(ring.try_pop(out));
        EXPECT_EQ(out, i);
    }
    EXPECT_TRUE(ring.empty_approx());
}

TEST(SpscRing, SmallestCapacityTwoWorks) {
    auto ring = make_ring(2);
    ASSERT_TRUE(ring.try_push(1));
    ASSERT_TRUE(ring.try_push(2));
    EXPECT_FALSE(ring.try_push(3));
    std::uint64_t out = 0;
    ASSERT_TRUE(ring.try_pop(out));
    EXPECT_EQ(out, 1U);
    ASSERT_TRUE(ring.try_push(3));  // reuses slot 0
    ASSERT_TRUE(ring.try_pop(out));
    EXPECT_EQ(out, 2U);
    ASSERT_TRUE(ring.try_pop(out));
    EXPECT_EQ(out, 3U);
}

// ---- FIFO across many slot wraps
// ------------------------------------------------------------------
class SpscRingFifo : public ::testing::TestWithParam<std::size_t> {};

TEST_P(SpscRingFifo, KeepsOrderAcrossManyLaps) {
    const std::size_t capacity = GetParam();
    auto ring = make_ring(capacity);
    std::uint64_t next_in = 0;
    std::uint64_t next_out = 0;
    for (int lap = 0; lap < 100; ++lap) {
        // Vary the fill level each lap so slot indices wrap at different points.
        const std::size_t batch = 1 + (static_cast<std::size_t>(lap) % capacity);
        for (std::size_t i = 0; i < batch; ++i) {
            ASSERT_TRUE(ring.try_push(next_in++));
        }
        EXPECT_EQ(ring.size_approx(), batch);
        for (std::size_t i = 0; i < batch; ++i) {
            std::uint64_t out = 0;
            ASSERT_TRUE(ring.try_pop(out));
            ASSERT_EQ(out, next_out++);
        }
    }
    EXPECT_TRUE(ring.empty_approx());
}

INSTANTIATE_TEST_SUITE_P(Capacities, SpscRingFifo, ::testing::Values(2U, 4U, 8U));

// ---- claim / commit, peek / release
// --------------------------------------------------------------
TEST(SpscRing, ClaimTwiceReturnsSameSlotAndCommitPublishes) {
    auto ring = make_ring(4);
    std::uint64_t* slot = ring.try_claim();
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(ring.try_claim(), slot);
    *slot = 7;
    EXPECT_TRUE(ring.empty_approx());  // not published yet
    ring.commit();
    EXPECT_EQ(ring.size_approx(), 1U);
    const std::uint64_t* peeked = ring.try_peek();
    ASSERT_NE(peeked, nullptr);
    EXPECT_EQ(*peeked, 7U);
}

TEST(SpscRing, PeekTwiceReturnsSameSlotAndReleaseFrees) {
    auto ring = make_ring(2);
    ASSERT_TRUE(ring.try_push(1));
    ASSERT_TRUE(ring.try_push(2));
    const std::uint64_t* first = ring.try_peek();
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(ring.try_peek(), first);
    EXPECT_EQ(*first, 1U);
    EXPECT_FALSE(ring.try_push(3));  // still full until release
    ring.release();
    EXPECT_TRUE(ring.try_push(3));
    const std::uint64_t* second = ring.try_peek();
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(*second, 2U);
}

TEST(SpscRing, InterleavedPushClaimPopPeek) {
    auto ring = make_ring(8);
    ASSERT_TRUE(ring.try_push(10));
    std::uint64_t* slot = ring.try_claim();
    ASSERT_NE(slot, nullptr);
    *slot = 11;
    ring.commit();
    ASSERT_TRUE(ring.try_push(12));

    std::uint64_t out = 0;
    ASSERT_TRUE(ring.try_pop(out));
    EXPECT_EQ(out, 10U);
    const std::uint64_t* peeked = ring.try_peek();
    ASSERT_NE(peeked, nullptr);
    EXPECT_EQ(*peeked, 11U);
    ring.release();
    ASSERT_TRUE(ring.try_pop(out));
    EXPECT_EQ(out, 12U);
    EXPECT_TRUE(ring.empty_approx());
}

// ---- element types and ownership
// -----------------------------------------------------------------
struct Payload64 {
    std::array<std::uint64_t, 8> words;
};

TEST(SpscRing, CarriesMultiWordPayloadsIntact) {
    auto created = SpscRing<Payload64>::create(4);
    ASSERT_TRUE(created.has_value());
    auto ring = std::move(created).value();
    Payload64 in{};
    for (std::uint64_t i = 0; i < 8; ++i) {
        in.words.at(i) = 0x0101'0101'0101'0101ULL * (i + 1);
    }
    ASSERT_TRUE(ring.try_push(in));
    Payload64 out{};
    ASSERT_TRUE(ring.try_pop(out));
    EXPECT_EQ(out.words, in.words);
}

TEST(SpscRing, MovedRingKeepsItsContents) {
    auto ring = make_ring(4);
    ASSERT_TRUE(ring.try_push(5));
    Ring moved = std::move(ring);
    std::uint64_t out = 0;
    ASSERT_TRUE(moved.try_pop(out));
    EXPECT_EQ(out, 5U);
}

// ---- size_approx under concurrency
// ---------------------------------------------------------------
TEST(SpscRing, SizeApproxStaysWithinCapacityUnderConcurrency) {
    constexpr std::uint64_t kItems = 200'000;
    auto ring = make_ring(64);
    auto& view = ring.view();
    std::atomic<bool> done{false};
    std::size_t max_seen = 0;

    std::thread observer([&] {
        while (!done.load(std::memory_order_acquire)) {
            max_seen = std::max(max_seen, view.size_approx());
        }
    });
    std::thread consumer([&] {
        std::uint64_t expected = 0;
        while (expected < kItems) {
            std::uint64_t out = 0;
            if (view.try_pop(out)) {
                EXPECT_EQ(out, expected);  // EXPECT: an early return would hang the producer
                ++expected;
            }
        }
    });
    for (std::uint64_t i = 0; i < kItems;) {
        if (view.try_push(i)) {
            ++i;
        }
    }
    consumer.join();
    done.store(true, std::memory_order_release);
    observer.join();
    EXPECT_LE(max_seen, 64U);
    EXPECT_TRUE(view.empty_approx());
}

// ---- debug misuse detection
// ----------------------------------------------------------------------
class SpscRingDeathTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!kDebugChecks) {
            GTEST_SKIP() << "misuse checks are compiled out in release builds";
        }
        GTEST_FLAG_SET(death_test_style, "threadsafe");
    }
};

TEST_F(SpscRingDeathTest, CommitWithoutClaimFailsFast) {
    EXPECT_DEATH(
        {
            auto ring = make_ring(4);
            ring.commit();
        },
        "commit\\(\\) without try_claim\\(\\)");
}

TEST_F(SpscRingDeathTest, ReleaseWithoutPeekFailsFast) {
    EXPECT_DEATH(
        {
            auto ring = make_ring(4);
            ring.release();
        },
        "release\\(\\) without try_peek\\(\\)");
}

TEST_F(SpscRingDeathTest, SecondProducerThreadFailsFast) {
    EXPECT_DEATH(
        {
            auto ring = make_ring(4);
            static_cast<void>(ring.try_push(1));
            std::thread other([&] { static_cast<void>(ring.try_push(2)); });
            other.join();
        },
        "producer used from two threads");
}

TEST_F(SpscRingDeathTest, SecondConsumerThreadFailsFast) {
    EXPECT_DEATH(
        {
            auto ring = make_ring(4);
            static_cast<void>(ring.try_push(1));
            static_cast<void>(ring.try_peek());
            std::thread other([&] { static_cast<void>(ring.try_peek()); });
            other.join();
        },
        "consumer used from two threads");
}

TEST(SpscRing, OneThreadMayBeProducerAndConsumer) {
    auto ring = make_ring(2);
    for (std::uint64_t i = 0; i < 10; ++i) {
        ASSERT_TRUE(ring.try_push(i));
        std::uint64_t out = 0;
        ASSERT_TRUE(ring.try_pop(out));
        EXPECT_EQ(out, i);
    }
}

}  // namespace
