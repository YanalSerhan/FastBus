// SPSC ring stress tests (M1.7). One producer and one consumer thread move patterned 64-byte
// payloads through rings of capacity 2 / 64 / 4096 in several regimes. The consumer checks
// order (no loss, no duplication) and that every payload is internally consistent (no torn
// reads). Each test is duration-bounded and also has a ctest TIMEOUT.
//
// Item counts: FASTBUS_STRESS_REDUCED (set by CMake for sanitizer and coverage builds) divides
// them by 10; FASTBUS_STRESS_MULTIPLIER (CMake cache variable, default 1) scales them for soak
// runs.
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <ostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "fastbus/core/clock.hpp"
#include "fastbus/core/spsc_ring.hpp"
#include "fastbus/core/time.hpp"
#include "fastbus/platform/cpu.hpp"
#include "fastbus/platform/thread.hpp"

namespace {

namespace core = fastbus::core;
namespace platform = fastbus::platform;

#if defined(FASTBUS_STRESS_REDUCED)
constexpr std::uint64_t kScaleDivisor = 10;
#else
constexpr std::uint64_t kScaleDivisor = 1;
#endif
#if !defined(FASTBUS_STRESS_MULTIPLIER)
#define FASTBUS_STRESS_MULTIPLIER 1  // NOLINT(cppcoreguidelines-macro-usage): set by CMake
#endif
constexpr std::uint64_t kMultiplier = FASTBUS_STRESS_MULTIPLIER;
constexpr core::Duration kTimeLimit = core::Duration::seconds(30);

constexpr std::uint64_t scaled(std::uint64_t items) {
    return std::max<std::uint64_t>(1'000, items * kMultiplier / kScaleDivisor);
}

// ---- Payload: 8 words, all derived from the sequence number --------------------------------
struct Payload {
    std::array<std::uint64_t, 8> words;
};
static_assert(sizeof(Payload) == 64);

constexpr std::uint64_t pattern(std::uint64_t seq, std::size_t word) {
    // Distinct, non-zero, sequence-dependent value per word (golden-ratio mixing).
    return ((seq + 1) * 0x9E37'79B9'7F4A'7C15ULL) ^ (static_cast<std::uint64_t>(word) << 56U);
}

void fill(Payload& p, std::uint64_t seq) {
    for (std::size_t w = 0; w < p.words.size(); ++w) {
        p.words.at(w) = pattern(seq, w);
    }
}

bool consistent(const Payload& p, std::uint64_t seq) {
    for (std::size_t w = 0; w < p.words.size(); ++w) {
        if (p.words.at(w) != pattern(seq, w)) {
            return false;
        }
    }
    return true;
}

// ---- Delays: cheap per-thread xorshift so injected jitter costs almost nothing --------------
class Jitter {
public:
    explicit Jitter(std::uint64_t seed) noexcept : state_(seed | 1U) {}

    /// With probability ~1/one_in, spins briefly or yields.
    void maybe_delay(std::uint32_t one_in) noexcept {
        if (one_in == 0 || next() % one_in != 0) {
            return;
        }
        if ((next() & 7U) == 0) {
            std::this_thread::yield();
        } else {
            spin(static_cast<std::uint32_t>(next() % 64));
        }
    }
    static void spin(std::uint32_t n) noexcept {
        for (std::uint32_t i = 0; i < n; ++i) {
            platform::cpu_relax();
        }
    }

private:
    std::uint64_t next() noexcept {
        state_ ^= state_ << 13U;
        state_ ^= state_ >> 7U;
        state_ ^= state_ << 17U;
        return state_;
    }
    std::uint64_t state_;
};

// ---- Test parameters ---------------------------------------------------------------------------
enum class Api : std::uint8_t { Copy, ZeroCopy };

struct Regime {
    const char* name;
    std::uint64_t items;  // before scaling
    Api api;
    std::uint32_t producer_jitter;  // 0 = none, else ~1 delay per N operations
    std::uint32_t consumer_jitter;
    std::uint32_t producer_spin;  // fixed cpu_relax count per item (slow side)
    std::uint32_t consumer_spin;
    bool pin;
};

constexpr std::array kRegimes{
    Regime{.name = "Balanced",
           .items = 10'000'000,
           .api = Api::Copy,
           .producer_jitter = 0,
           .consumer_jitter = 0,
           .producer_spin = 0,
           .consumer_spin = 0,
           .pin = false},
    Regime{.name = "BalancedZeroCopy",
           .items = 10'000'000,
           .api = Api::ZeroCopy,
           .producer_jitter = 0,
           .consumer_jitter = 0,
           .producer_spin = 0,
           .consumer_spin = 0,
           .pin = false},
    Regime{.name = "RandomJitter",
           .items = 1'000'000,
           .api = Api::Copy,
           .producer_jitter = 16,
           .consumer_jitter = 16,
           .producer_spin = 0,
           .consumer_spin = 0,
           .pin = false},
    Regime{.name = "SlowConsumer",
           .items = 1'000'000,
           .api = Api::ZeroCopy,
           .producer_jitter = 0,
           .consumer_jitter = 0,
           .producer_spin = 0,
           .consumer_spin = 32,
           .pin = false},
    Regime{.name = "SlowProducer",
           .items = 1'000'000,
           .api = Api::Copy,
           .producer_jitter = 0,
           .consumer_jitter = 0,
           .producer_spin = 32,
           .consumer_spin = 0,
           .pin = false},
    Regime{.name = "Pinned",
           .items = 2'000'000,
           .api = Api::Copy,
           .producer_jitter = 0,
           .consumer_jitter = 0,
           .producer_spin = 0,
           .consumer_spin = 0,
           .pin = true},
};

struct Params {
    std::size_t capacity;
    Regime regime;
};

// Readable parameter names in failure messages (gtest finds this by ADL).
// NOLINTNEXTLINE(readability-identifier-naming): name required by GoogleTest
[[maybe_unused]] void PrintTo(const Params& p, std::ostream* os) {
    *os << p.regime.name << " capacity " << p.capacity;
}

// ---- Shared outcome (written by the consumer, read after join) ----------------------------------
struct Outcome {
    std::uint64_t received = 0;
    std::uint64_t out_of_order = 0;  // wrong sequence number
    std::uint64_t torn = 0;          // right sequence, inconsistent words
    std::uint64_t first_bad = 0;
};

void check_item(const Payload& p, Outcome& out) {
    const std::uint64_t expected = out.received++;
    if (consistent(p, expected)) {
        return;
    }
    // Word 0 wrong: a different item arrived (loss, duplication or reordering). Word 0 right but
    // another word wrong: the right item, read while it was still being written (torn).
    const bool right_item = p.words[0] == pattern(expected, 0);
    std::uint64_t& counter = right_item ? out.torn : out.out_of_order;
    if (out.torn + out.out_of_order == 0) {
        out.first_bad = expected;
    }
    ++counter;
}

class SpscRingStress : public ::testing::TestWithParam<Params> {};

TEST_P(SpscRingStress, DeliversEveryItemInOrderWithoutTornPayloads) {
    const Params params = GetParam();
    const std::size_t capacity = params.capacity;
    const Regime regime = params.regime;
    const std::uint64_t items = scaled(regime.items);
    if (regime.pin && platform::hardware_concurrency() < 2) {
        GTEST_SKIP() << "pinning variant needs at least 2 CPUs";
    }

    auto created = core::SpscRing<Payload>::create(capacity);
    ASSERT_TRUE(created.has_value());
    auto ring = std::move(created).value();
    auto& view = ring.view();

    const core::Timestamp deadline = core::SystemMonotonicClock::read() + kTimeLimit;
    std::atomic<std::uint64_t> produced{0};
    std::atomic<bool> producer_done{false};
    std::atomic<bool> pin_refused{false};
    Outcome outcome;

    std::thread consumer([&] {
        if (regime.pin && !platform::pin_current_thread(1)) {
            pin_refused.store(true, std::memory_order_relaxed);
        }
        Jitter jitter{0xC0FFEEULL + capacity};
        Payload p{};
        while (true) {
            bool got = false;
            if (regime.api == Api::Copy) {
                got = view.try_pop(p);
                if (got) {
                    check_item(p, outcome);
                }
            } else if (const Payload* slot = view.try_peek(); slot != nullptr) {
                check_item(*slot, outcome);
                view.release();
                got = true;
            }
            if (got) {
                Jitter::spin(regime.consumer_spin);
                jitter.maybe_delay(regime.consumer_jitter);
            } else if (producer_done.load(std::memory_order_acquire) &&
                       outcome.received == produced.load(std::memory_order_relaxed)) {
                break;  // producer finished and everything it pushed has been consumed
            } else {
                platform::cpu_relax();
            }
        }
    });

    std::thread producer([&] {
        if (regime.pin && !platform::pin_current_thread(0)) {
            pin_refused.store(true, std::memory_order_relaxed);
        }
        Jitter jitter{0xBADC0DEULL + capacity};
        std::uint64_t seq = 0;
        while (seq < items) {
            // Check the deadline every 4096 attempts: cheap, and bounds the test's run time.
            if ((seq & 4095U) == 0 && core::SystemMonotonicClock::read() > deadline) {
                break;
            }
            bool pushed = false;
            if (regime.api == Api::Copy) {
                Payload p{};
                fill(p, seq);
                pushed = view.try_push(p);
            } else if (Payload* slot = view.try_claim(); slot != nullptr) {
                fill(*slot, seq);
                view.commit();
                pushed = true;
            }
            if (pushed) {
                ++seq;
                Jitter::spin(regime.producer_spin);
                jitter.maybe_delay(regime.producer_jitter);
            } else {
                platform::cpu_relax();
            }
        }
        produced.store(seq, std::memory_order_relaxed);
        producer_done.store(true, std::memory_order_release);
    });

    producer.join();
    consumer.join();

    const std::uint64_t sent = produced.load();
    RecordProperty("items_requested", std::to_string(items));
    RecordProperty("items_transferred", std::to_string(sent));
    if (sent < items) {
        std::cout << "[ NOTE     ] time limit reached after " << sent << " of " << items
                  << " items\n";
    }
    EXPECT_EQ(outcome.received, sent) << "lost or duplicated items";
    EXPECT_EQ(outcome.out_of_order, 0U) << "first bad item: " << outcome.first_bad;
    EXPECT_EQ(outcome.torn, 0U) << "first torn item: " << outcome.first_bad;
    EXPECT_TRUE(view.empty_approx());
    EXPECT_GE(sent, std::min<std::uint64_t>(items, 1'000)) << "made almost no progress";
    if (pin_refused.load()) {
        std::cout << "[ NOTE     ] the OS refused CPU pinning; ran unpinned\n";
    }
}

std::vector<Params> all_params() {
    std::vector<Params> out;
    for (const std::size_t capacity : {2U, 64U, 4096U}) {
        for (const Regime& r : kRegimes) {
            out.push_back(Params{.capacity = capacity, .regime = r});
        }
    }
    return out;
}

INSTANTIATE_TEST_SUITE_P(Regimes, SpscRingStress, ::testing::ValuesIn(all_params()),
                         [](const ::testing::TestParamInfo<Params>& param_info) {
                             return std::string{param_info.param.regime.name} + "_cap" +
                                    std::to_string(param_info.param.capacity);
                         });

}  // namespace
