#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <type_traits>

#include "fastbus/core/clock.hpp"
#include "fastbus/core/sleep.hpp"
#include "fastbus/core/time.hpp"
#include "fastbus/platform/cpu.hpp"
#include "fastbus/platform/time_source.hpp"

namespace {

using fastbus::core::Clock;
using fastbus::core::Duration;
using fastbus::core::ManualClock;
using fastbus::core::SystemMonotonicClock;
using fastbus::core::Timestamp;
using fastbus::core::WallClock;

static_assert(std::is_abstract_v<Clock>);
static_assert(!std::is_copy_constructible_v<ManualClock>, "holds an atomic");

// Timing tests use only *lower* bounds with generous slack: CI machines are noisy, and an upper
// bound on sleep duration would be flaky (docs/known-issues.md, risk R4).
constexpr auto kSleep = Duration::milliseconds(5);
constexpr auto kSlack = Duration::milliseconds(1);

TEST(SystemMonotonicClock, NeverGoesBackwards) {
    constexpr int kCalls = 1'000'000;
    Timestamp previous = SystemMonotonicClock::read();
    for (int i = 0; i < kCalls; ++i) {
        const Timestamp current = SystemMonotonicClock::read();
        ASSERT_GE(current, previous) << "at call " << i;
        previous = current;
    }
}

TEST(SystemMonotonicClock, AdvancesAcrossASleep) {
    const SystemMonotonicClock clock;
    const Timestamp before = clock.now();
    fastbus::core::sleep_for(kSleep);
    EXPECT_GE(clock.now() - before, kSleep - kSlack);
}

TEST(SystemMonotonicClock, ReportsResolution) {
    const Duration res = SystemMonotonicClock::resolution();
    RecordProperty("monotonic_resolution_ns", std::to_string(res.count_ns()));
    EXPECT_GE(res, Duration::nanoseconds(1));
    EXPECT_LE(res, Duration::microseconds(1)) << "QPC and CLOCK_MONOTONIC are sub-microsecond";
}

TEST(WallClock, IsAfter2020AndUsesTheUnixEpoch) {
    constexpr std::int64_t kJan2020Ns = 1'577'836'800LL * 1'000'000'000LL;
    EXPECT_GT(WallClock{}.now().ns_since_epoch(), kJan2020Ns);
}

TEST(ManualClock, OnlyMovesWhenTold) {
    ManualClock clock{Timestamp::from_ns(100)};
    EXPECT_EQ(clock.now(), Timestamp::from_ns(100));
    fastbus::core::sleep_for(Duration::milliseconds(1));
    EXPECT_EQ(clock.now(), Timestamp::from_ns(100)) << "real time must not leak in";
    clock.advance(Duration::nanoseconds(50));
    EXPECT_EQ(clock.now(), Timestamp::from_ns(150));
    clock.set(Timestamp::from_ns(7));
    EXPECT_EQ(clock.now(), Timestamp::from_ns(7));
}

TEST(ManualClock, WorksThroughTheClockInterface) {
    const ManualClock manual{Timestamp::from_ns(42)};
    const Clock& clock = manual;
    EXPECT_EQ(clock.now(), Timestamp::from_ns(42));
}

TEST(ManualClock, ConcurrentAdvanceAndReadIsMonotonic) {
    constexpr int kSteps = 100'000;
    ManualClock clock;
    std::atomic<bool> done{false};
    std::thread writer([&] {
        for (int i = 0; i < kSteps; ++i) {
            clock.advance(Duration::nanoseconds(1));
        }
        done.store(true, std::memory_order_release);
    });
    Timestamp previous = clock.now();
    while (!done.load(std::memory_order_acquire)) {
        const Timestamp current = clock.now();
        ASSERT_GE(current, previous);
        previous = current;
    }
    writer.join();
    EXPECT_EQ(clock.now(), Timestamp::from_ns(kSteps));
}

TEST(PreciseSleepUntil, ReturnsNoEarlierThanTheDeadline) {
    const SystemMonotonicClock clock;
    const Timestamp deadline = clock.now() + kSleep;
    fastbus::core::precise_sleep_until(clock, deadline);
    EXPECT_GE(clock.now(), deadline);
}

TEST(PreciseSleepUntil, PastDeadlineReturnsImmediately) {
    const SystemMonotonicClock clock;
    const Timestamp start = clock.now();
    fastbus::core::precise_sleep_until(clock, start - Duration::seconds(1));
    EXPECT_LT(clock.now() - start, Duration::seconds(1));
}

TEST(PreciseSleepUntil, FollowsAManualClockAdvancedByAnotherThread) {
    ManualClock clock;
    const Timestamp deadline = Timestamp::from_ns(1'000);
    std::thread advancer([&] {
        for (int i = 0; i < 10; ++i) {
            fastbus::core::sleep_for(Duration::microseconds(100));
            clock.advance(Duration::nanoseconds(100));
        }
    });
    fastbus::core::precise_sleep_until(clock, deadline, Duration{});
    EXPECT_GE(clock.now(), deadline);
    advancer.join();
}

TEST(SleepFor, NonPositiveDurationsReturnImmediately) {
    const SystemMonotonicClock clock;
    const Timestamp start = clock.now();
    fastbus::core::sleep_for(Duration{});
    fastbus::core::sleep_for(Duration::milliseconds(-5));
    EXPECT_LT(clock.now() - start, Duration::milliseconds(100));
}

TEST(Platform, CpuRelaxAndTimerScopeAreCallable) {
    fastbus::platform::cpu_relax();
    const fastbus::platform::HighResolutionTimerScope scope;
    RecordProperty("high_resolution_timer_active", scope.active() ? "true" : "false");
    SUCCEED();
}

}  // namespace
