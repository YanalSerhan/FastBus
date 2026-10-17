// SPSC ring micro-benchmarks (M1.9) against the mutex + deque baseline.
//
//   *_PushPop       one thread: push one item, pop it again (cost of the operations themselves)
//   *_TwoThreads    producer = benchmark thread, consumer = helper thread; items/s throughput
//
// Numbers from a laptop or a shared CI runner are indicative only; the M9 methodology (ADR-011)
// pins cores, repeats runs and reports medians. Payloads: 8 bytes and 64 bytes.
#include <benchmark/benchmark.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <type_traits>
#include <utility>

#include "fastbus/core/spsc_ring.hpp"
#include "fastbus/platform/cpu.hpp"
#include "fastbus/platform/thread.hpp"

#include "baseline_mutex_queue.hpp"

namespace {

namespace core = fastbus::core;
namespace platform = fastbus::platform;
using fastbus::bench::MutexQueue;

struct Payload64 {
    std::array<std::uint64_t, 8> words;
};

constexpr std::size_t kCapacity = 1024;
constexpr std::uint64_t kBatch = 1U << 16U;  // items per benchmark iteration (two-thread runs)

template <typename T>
T make_value(std::uint64_t i) {
    T value{};
    if constexpr (std::is_same_v<T, std::uint64_t>) {
        value = i;
    } else {
        value.words[0] = i;
    }
    return value;
}

// Adapters so both queues share the benchmark bodies.
template <typename T>
struct RingQueue {
    core::SpscRing<T> ring = std::move(core::SpscRing<T>::create(kCapacity)).value();
    bool try_push(const T& v) noexcept { return ring.try_push(v); }
    bool try_pop(T& v) noexcept { return ring.try_pop(v); }
};
template <typename T>
struct LockedQueue {
    MutexQueue<T> queue{kCapacity};
    bool try_push(const T& v) { return queue.try_push(v); }
    bool try_pop(T& v) { return queue.try_pop(v); }
};

// ---- Single thread ---------------------------------------------------------------------------
template <typename Queue, typename T>
void push_pop(benchmark::State& state) {
    Queue q;
    T out{};
    std::uint64_t i = 0;
    for (auto _ : state) {
        benchmark::DoNotOptimize(q.try_push(make_value<T>(i++)));
        benchmark::DoNotOptimize(q.try_pop(out));
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(sizeof(T)));
}

// ---- Two threads -----------------------------------------------------------------------------
template <typename Queue, typename T>
void two_threads(benchmark::State& state) {
    Queue q;
    std::atomic<std::uint64_t> consumed{0};
    std::atomic<bool> stop{false};
    const bool pin = platform::hardware_concurrency() >= 2;

    std::thread consumer([&] {
        if (pin) {
            static_cast<void>(platform::pin_current_thread(1));
        }
        T out{};
        std::uint64_t n = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            if (q.try_pop(out)) {
                consumed.store(++n, std::memory_order_release);
            } else {
                platform::cpu_relax();
            }
        }
    });
    if (pin) {
        static_cast<void>(platform::pin_current_thread(0));
    }

    std::uint64_t sent = 0;
    for (auto _ : state) {
        const std::uint64_t target = sent + kBatch;
        while (sent < target) {
            if (q.try_push(make_value<T>(sent))) {
                ++sent;
            } else {
                platform::cpu_relax();
            }
        }
        while (consumed.load(std::memory_order_acquire) < sent) {  // count only delivered items
            platform::cpu_relax();
        }
    }
    stop.store(true, std::memory_order_relaxed);
    consumer.join();
    static_cast<void>(platform::unpin_current_thread());
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kBatch));
    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(kBatch * sizeof(T)));
    state.counters["pinned"] = pin ? 1 : 0;
}

void BM_SpscRing_PushPop_u64(benchmark::State& s) {
    push_pop<RingQueue<std::uint64_t>, std::uint64_t>(s);
}
void BM_MutexQueue_PushPop_u64(benchmark::State& s) {
    push_pop<LockedQueue<std::uint64_t>, std::uint64_t>(s);
}
void BM_SpscRing_PushPop_64B(benchmark::State& s) {
    push_pop<RingQueue<Payload64>, Payload64>(s);
}
void BM_MutexQueue_PushPop_64B(benchmark::State& s) {
    push_pop<LockedQueue<Payload64>, Payload64>(s);
}
void BM_SpscRing_TwoThreads_u64(benchmark::State& s) {
    two_threads<RingQueue<std::uint64_t>, std::uint64_t>(s);
}
void BM_MutexQueue_TwoThreads_u64(benchmark::State& s) {
    two_threads<LockedQueue<std::uint64_t>, std::uint64_t>(s);
}
void BM_SpscRing_TwoThreads_64B(benchmark::State& s) {
    two_threads<RingQueue<Payload64>, Payload64>(s);
}
void BM_MutexQueue_TwoThreads_64B(benchmark::State& s) {
    two_threads<LockedQueue<Payload64>, Payload64>(s);
}

BENCHMARK(BM_SpscRing_PushPop_u64);
BENCHMARK(BM_MutexQueue_PushPop_u64);
BENCHMARK(BM_SpscRing_PushPop_64B);
BENCHMARK(BM_MutexQueue_PushPop_64B);
BENCHMARK(BM_SpscRing_TwoThreads_u64)->UseRealTime()->Unit(benchmark::kMillisecond);
BENCHMARK(BM_MutexQueue_TwoThreads_u64)->UseRealTime()->Unit(benchmark::kMillisecond);
BENCHMARK(BM_SpscRing_TwoThreads_64B)->UseRealTime()->Unit(benchmark::kMillisecond);
BENCHMARK(BM_MutexQueue_TwoThreads_64B)->UseRealTime()->Unit(benchmark::kMillisecond);

}  // namespace
