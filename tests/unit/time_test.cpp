#include <gtest/gtest.h>

#include <cstdint>
#include <type_traits>

#include "fastbus/core/time.hpp"

namespace {

using fastbus::core::Duration;
using fastbus::core::Timestamp;

// --- Only meaningful arithmetic compiles -----------------------------------------------------
template <typename A, typename B>
concept Addable = requires(A a, B b) { a + b; };
template <typename A, typename B>
concept Subtractable = requires(A a, B b) { a - b; };
template <typename A, typename B>
concept Multipliable = requires(A a, B b) { (a * b); };

static_assert(Addable<Timestamp, Duration>);
static_assert(Addable<Duration, Timestamp>);
static_assert(Addable<Duration, Duration>);
static_assert(!Addable<Timestamp, Timestamp>, "adding two points in time is meaningless");
static_assert(!Addable<Timestamp, std::int64_t>, "raw integers are not durations");
static_assert(Subtractable<Timestamp, Timestamp>);
static_assert(std::is_same_v<decltype(Timestamp{} - Timestamp{}), Duration>);
static_assert(!Subtractable<Duration, Timestamp>);
static_assert(!Multipliable<Timestamp, std::int64_t>);
static_assert(!std::is_convertible_v<std::int64_t, Duration>);
static_assert(!std::is_convertible_v<std::int64_t, Timestamp>);

// --- Layout -------------------------------------------------------------------------------------
static_assert(std::is_trivially_copyable_v<Duration> && sizeof(Duration) == sizeof(std::int64_t));
static_assert(std::is_trivially_copyable_v<Timestamp> && sizeof(Timestamp) == 8);

// --- constexpr unit conversions ------------------------------------------------------------------
static_assert(Duration::microseconds(1).count_ns() == 1'000);
static_assert(Duration::milliseconds(1).count_ns() == 1'000'000);
static_assert(Duration::seconds(2).count_ns() == 2'000'000'000);
static_assert(Duration{}.count_ns() == 0);

TEST(Duration, ArithmeticAndComparison) {
    const auto a = Duration::milliseconds(3);
    const auto b = Duration::microseconds(500);
    EXPECT_EQ((a + b).count_ns(), 3'500'000);
    EXPECT_EQ((a - b).count_ns(), 2'500'000);
    EXPECT_EQ((-b).count_ns(), -500'000);
    EXPECT_EQ((b * 4).count_ns(), 2'000'000);
    EXPECT_EQ((4 * b), b * 4);
    EXPECT_LT(b, a);
    EXPECT_GT(Duration::nanoseconds(1), Duration::nanoseconds(-1));
}

TEST(Timestamp, ArithmeticWithDurations) {
    const auto t0 = Timestamp::from_ns(1'000);
    const auto d = Duration::nanoseconds(250);
    EXPECT_EQ((t0 + d).ns_since_epoch(), 1'250);
    EXPECT_EQ((d + t0), (t0 + d));
    EXPECT_EQ((t0 - d).ns_since_epoch(), 750);
    EXPECT_EQ((t0 + d) - t0, d);
    EXPECT_EQ(t0 - (t0 + d), -d);
}

TEST(Timestamp, OrderingIsByTime) {
    const auto early = Timestamp::from_ns(-5);  // before the epoch is representable
    const auto late = Timestamp::from_ns(5);
    EXPECT_LT(early, late);
    EXPECT_EQ(Timestamp{}, Timestamp::from_ns(0));
}

}  // namespace
