# Google Benchmark suites. M0 has only a smoke benchmark proving the harness runs in CI.
fastbus_add_benchmark(fastbus_bm_smoke
    SOURCES bm_smoke.cpp
    DEPS fastbus::core)
