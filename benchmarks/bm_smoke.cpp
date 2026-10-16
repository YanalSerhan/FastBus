// Smoke benchmark: proves the benchmark harness builds, links and runs (M0.6).
// Real suites start in M1 (bm_spsc_ring.cpp).
#include <benchmark/benchmark.h>

#include "fastbus/core/build_info.hpp"

namespace {

void BM_LibraryVersion(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(fastbus::core::library_version());
    }
}
BENCHMARK(BM_LibraryVersion);

}  // namespace
