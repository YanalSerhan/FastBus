# Google Benchmark suites. M0 has only a smoke benchmark proving the harness runs in CI.
fastbus_add_benchmark(fastbus_bm_smoke
    SOURCES bm_smoke.cpp
    DEPS fastbus::core)

# M1.9: SPSC ring vs the mutex + deque baseline (baseline_mutex_queue.hpp, kept for M9).
fastbus_add_benchmark(fastbus_bm_spsc_ring
    SOURCES bm_spsc_ring.cpp
    DEPS fastbus::core)
