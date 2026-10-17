# Benchmarks

> Placeholder. Methodology (ADR-011, `PRD_benchmarks.md`), hardware table, results and charts are added in M9.

M0 has a single smoke benchmark (`benchmarks/bm_smoke.cpp`) that CI runs with `--benchmark_min_time=0.01s` to prove the harness works.

## M1 — SPSC ring vs `std::mutex + std::deque` (indicative)

`benchmarks/bm_spsc_ring.cpp`; baseline in `benchmarks/baseline_mutex_queue.hpp` (bounded to the same capacity, kept for M9). Capacity 1024. Two-thread runs move batches of 65 536 items and count only *delivered* items; producer pinned to CPU 0 and consumer to CPU 1 when the OS allows.

Environment: 2-vCPU virtual machine (2.1 GHz), Linux, GCC 13 `-O3 -DNDEBUG`, Google Benchmark 1.9.4, `--benchmark_min_time=0.5s --benchmark_repetitions=3`, medians shown, 2026-10-17. **Not publishable numbers:** a shared VM, no isolated cores, no frequency control. M9 repeats this with the ADR-011 methodology.

| Benchmark | SPSC ring | mutex + deque | Ratio |
|---|---|---|---|
| Single thread push + pop, 8 B | 2.0 ns | 16.2 ns | 8× |
| Single thread push + pop, 64 B | 11.3 ns | 21.1 ns | 1.9× |
| Two threads, 8 B | 279 M items/s | 2.4 M items/s | 114× |
| Two threads, 64 B | 21.8 M items/s (1.3 GiB/s) | 2.8 M items/s | 7.7× |

Reproduce: `cmake --preset linux-bench && cmake --build --preset linux-bench && ./build/linux-bench/bin/fastbus_bm_spsc_ring --benchmark_repetitions=3 --benchmark_report_aggregates_only=true`, or `.\scripts\dev.ps1 bench` on Windows.
