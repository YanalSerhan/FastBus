#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "fastbus/platform/thread.hpp"

namespace {

namespace platform = fastbus::platform;

// Every test that changes thread state runs in its own std::thread, so pinning or renaming never
// leaks into the GoogleTest main thread and the other tests.
template <typename F>
void run_in_fresh_thread(F&& body) {
    std::thread t(std::forward<F>(body));
    t.join();
}

TEST(Thread, HardwareConcurrencyIsAtLeastOne) {
    const std::uint32_t n = platform::hardware_concurrency();
    RecordProperty("hardware_concurrency", std::to_string(n));
    EXPECT_GE(n, 1U);
}

TEST(Thread, ThreadIdIsStableWithinAThreadAndDiffersAcrossThreads) {
    const std::uint64_t main_id = platform::current_thread_id();
    EXPECT_EQ(platform::current_thread_id(), main_id);
    std::uint64_t other_id = 0;
    run_in_fresh_thread([&] { other_id = platform::current_thread_id(); });
    EXPECT_NE(other_id, main_id);
}

TEST(Thread, NameRoundTrips) {
    run_in_fresh_thread([] {
        ASSERT_TRUE(platform::set_current_thread_name("fb-worker"));
        std::array<char, platform::kMaxThreadNameLength + 1> name{};
        ASSERT_EQ(platform::current_thread_name(name), 9U);
        EXPECT_EQ(std::string_view{name.data()}, "fb-worker");
    });
}

TEST(Thread, LongNamesAreTruncatedAndNonAsciiIsReplaced) {
    run_in_fresh_thread([] {
        ASSERT_TRUE(platform::set_current_thread_name("a-very-long-thread-name-indeed"));
        std::array<char, platform::kMaxThreadNameLength + 1> name{};
        EXPECT_EQ(platform::current_thread_name(name), platform::kMaxThreadNameLength);
        EXPECT_EQ(std::string_view{name.data()}, "a-very-long-thr");

        ASSERT_TRUE(platform::set_current_thread_name("caf\xC3\xA9"));  // "café" in UTF-8
        ASSERT_EQ(platform::current_thread_name(name), 5U);
        EXPECT_EQ(std::string_view{name.data()}, "caf??");
    });
}

TEST(Thread, NameIntoTinyBufferIsTruncatedSafely) {
    run_in_fresh_thread([] {
        ASSERT_TRUE(platform::set_current_thread_name("abcdef"));
        std::array<char, 4> small{};
        EXPECT_EQ(platform::current_thread_name(small), 3U);
        EXPECT_EQ(std::string_view{small.data()}, "abc");
        EXPECT_EQ(platform::current_thread_name(std::span<char>{}), 0U);
    });
}

TEST(Thread, PinningMovesTheThreadToThatCpu) {
    run_in_fresh_thread([] {
        if (!platform::pin_current_thread(0)) {
            GTEST_SKIP() << "the OS refused CPU pinning here (container or policy)";
        }
        std::this_thread::yield();  // give the scheduler a chance to migrate us
        EXPECT_EQ(platform::current_cpu(), 0U);
        EXPECT_TRUE(platform::unpin_current_thread());
    });
}

TEST(Thread, PinningToANonexistentCpuFails) {
    run_in_fresh_thread([] {
        EXPECT_FALSE(platform::pin_current_thread(platform::hardware_concurrency() + 1'000U));
    });
}

TEST(Thread, CurrentCpuIsInRange) {
    EXPECT_LT(platform::current_cpu(), platform::hardware_concurrency() + 64U);
}

}  // namespace
