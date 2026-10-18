// Slab refcount stress test (M2.5): one publisher thread and N subscriber threads run the full
// ADR-003 cycle over a tiny pool, so every block is reused tens of thousands of times:
//   allocate -> fill -> share(N) -> descriptor to every subscriber (SPSC rings)
//   subscriber: verify payload -> release -> the Last releaser returns the index (SPSC ring)
//   publisher: drain return rings -> reclaim
// Checks: every subscriber sees every message in order with an intact payload (no block reused
// under a reader), every block comes back exactly once per message, and the pool ends full.
// Run under TSan in CI (memory-ordering.md section 13).
#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <thread>
#include <utility>
#include <vector>

#include "fastbus/core/slab.hpp"
#include "fastbus/core/spsc_ring.hpp"

namespace {

namespace core = fastbus::core;

// Five threads usually share fewer cores (CI runners have 2-4): waiting threads must give the
// CPU away, or they starve the threads they are waiting for.
void idle() {
    std::this_thread::yield();
}

#if defined(FASTBUS_STRESS_REDUCED)
constexpr std::uint64_t kMessages = 100'000;
#else
constexpr std::uint64_t kMessages = 1'000'000;
#endif
constexpr std::uint32_t kBlocks = 8;
constexpr std::uint32_t kSubscribers = 4;
constexpr std::size_t kPayload = 64;
constexpr std::size_t kWords = kPayload / sizeof(std::uint64_t);

constexpr std::uint64_t word(std::uint64_t seq, std::size_t i) {
    return (seq * 0x9E37'79B9'7F4A'7C15ULL) ^ i;
}

struct Subscriber {
    // Capacity >= pool size: at most kBlocks descriptors/returns are ever in flight.
    core::SpscRing<core::BlockHandle> descriptors =
        std::move(core::SpscRing<core::BlockHandle>::create(16)).value();
    core::SpscRing<std::uint32_t> returns =
        std::move(core::SpscRing<std::uint32_t>::create(16)).value();
    std::uint64_t received = 0;
    std::uint64_t bad_payloads = 0;
    std::uint64_t stale = 0;
};

TEST(SlabRefcountStress, EveryBlockReturnsExactlyOnceAndNoReaderSeesReuse) {
    const std::size_t bytes = core::SlabView::required_bytes(kBlocks, kPayload);
    struct Delete {
        void operator()(std::byte* p) const { ::operator delete(p, std::align_val_t{64}); }
    };
    const std::unique_ptr<std::byte, Delete> memory{
        static_cast<std::byte*>(::operator new(bytes, std::align_val_t{64}))};
    auto publisher_slab = core::SlabView::create({memory.get(), bytes}, kBlocks, kPayload).value();
    // Subscribers use their own attached view, as a separate process would.
    const auto subscriber_slab = core::SlabView::attach({memory.get(), bytes}).value();

    std::array<Subscriber, kSubscribers> subs;
    std::vector<std::thread> threads;
    threads.reserve(kSubscribers);
    for (Subscriber& s : subs) {
        threads.emplace_back([&s, slab = subscriber_slab]() mutable {
            while (s.received < kMessages) {
                core::BlockHandle h{};
                if (!s.descriptors.try_pop(h)) {
                    idle();
                    continue;
                }
                const auto payload = slab.payload(h);
                std::array<std::uint64_t, kWords> words{};
                if (payload.size() >= kPayload) {
                    std::memcpy(words.data(), payload.data(), kPayload);
                }
                for (std::size_t i = 0; i < kWords; ++i) {
                    s.bad_payloads += words.at(i) != word(s.received, i) ? 1U : 0U;
                }
                ++s.received;
                const core::ReleaseResult r = slab.release(h);
                if (r == core::ReleaseResult::Stale) {
                    ++s.stale;
                } else if (r == core::ReleaseResult::Last) {
                    while (!s.returns.try_push(h.index)) {
                        idle();
                    }
                }
            }
        });
    }

    std::uint64_t reclaimed = 0;
    std::uint64_t failed_reclaims = 0;
    const auto drain = [&] {
        for (Subscriber& s : subs) {
            std::uint32_t index = 0;
            while (s.returns.try_pop(index)) {
                if (publisher_slab.reclaim(index)) {
                    ++reclaimed;
                } else {
                    ++failed_reclaims;
                }
            }
        }
    };
    for (std::uint64_t seq = 0; seq < kMessages; ++seq) {
        auto h = publisher_slab.allocate();
        while (!h.has_value()) {  // pool exhausted: wait for returns (never blocks inside)
            drain();
            idle();
            h = publisher_slab.allocate();
        }
        const auto payload = publisher_slab.payload(*h);
        std::array<std::uint64_t, kWords> words{};
        for (std::size_t i = 0; i < kWords; ++i) {
            words.at(i) = word(seq, i);
        }
        std::memcpy(payload.data(), words.data(), kPayload);
        ASSERT_TRUE(publisher_slab.share(*h, kSubscribers));
        for (Subscriber& s : subs) {
            while (!s.descriptors.try_push(*h)) {
                idle();
            }
        }
        drain();
    }
    for (std::thread& t : threads) {
        t.join();
    }
    drain();

    EXPECT_EQ(reclaimed, kMessages) << "every message's block must come back exactly once";
    EXPECT_EQ(failed_reclaims, 0U);
    EXPECT_EQ(publisher_slab.free_count(), kBlocks);
    for (const Subscriber& s : subs) {
        EXPECT_EQ(s.received, kMessages);
        EXPECT_EQ(s.bad_payloads, 0U) << "a block was reused while a subscriber read it";
        EXPECT_EQ(s.stale, 0U);
    }
}

}  // namespace
