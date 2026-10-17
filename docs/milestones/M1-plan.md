# M1 — Platform layer + lock-free SPSC ring: plan

> **Status: approved 2026-10-17, in progress.** The plan, [PRD_spsc_ring.md](../PRD_spsc_ring.md), [ADR-001](../adr/ADR-001-error-handling.md), [ADR-007](../adr/ADR-007-clock-injection.md), [ADR-009](../adr/ADR-009-cache-line.md) and the open-question answers below were approved by the owner. Steps 1 (M1.1) and 2 (M1.2) done; step 3 (M1.3) awaiting Windows CI.

## Scope

Strong types and `Result<T, E>`; clocks (monotonic, wall, manual); thread utilities; an in-process single-producer/single-consumer ring designed as a **view over caller-provided memory**, so M2 can place the same code in shared memory; unit, edge-case and stress tests; TSan-clean; a written memory-ordering argument.

**Out of scope:** shared memory, processes, MPSC (M2); any blocking or wake-up mechanism; the public `api` module (M3).

## Acceptance criteria (from TODO.md)

1. The ring passes unit, edge-case and stress tests on MSVC, clang-cl, GCC and Clang; ASan, UBSan and TSan are clean.
2. `docs/memory-ordering.md` gives a complete happens-before argument for every atomic operation.
3. The coverage gate (≥ 85 % line coverage) is active in CI for `platform` + `core`.
4. No heap allocation inside push/pop, verified by a counting-allocator test.

## Work order (each step = one or more small commits, tests first or alongside)

| Step | TODO | Deliverable |
|---|---|---|
| 0 | M1.0 | This plan, PRD, ADR-001/007/009 approved |
| 1 | M1.1 | `strong_type.hpp`, ID types, `Timestamp`/`Duration`, `Result`/`Error` + tests |
| 2 | M1.2 | `Clock` interface, `SystemMonotonicClock`, `WallClock`, `ManualClock` (Windows: QPC; POSIX: `clock_gettime`) + tests |
| 3 | M1.3 | `cpu_relax`, affinity, thread name/id, `hardware_concurrency` + tests (skip where the OS forbids) |
| 4 | M1.4 | `cache_line.hpp`, `SpscRingLayout`, **`docs/memory-ordering.md` written before the ring code** |
| 5 | M1.5 | `SpscRingView` + `SpscRing` (push/pop, claim/commit, peek/release, debug misuse checks) |
| 6 | M1.6 | Unit + edge-case tests, including `uint8_t`/`uint16_t` index wrap-around |
| 7 | M1.7 | Stress tests (≥ 1e7 items, patterned 64-byte payloads, capacities 2/64/4096, slow-side variants) |
| 8 | M1.8 | Sanitizer runs (reduced iterations), coverage gate 85 % on, known-issues entry for TSan vs cross-process |
| 9 | M1.9 | Counting-allocator test; `bm_spsc_ring.cpp` vs a `mutex + deque` baseline |
| 10 | M1.10 | Milestone report, CHANGELOG, interview Q&As |

## Files

```
include/fastbus/core/      strong_type.hpp  ids.hpp  time.hpp  result.hpp  error.hpp  assert.hpp
                           clock.hpp  sleep.hpp  cache_line.hpp  spsc_ring_layout.hpp  spsc_ring.hpp
include/fastbus/platform/  time_source.hpp  cpu.hpp  thread.hpp
src/core/                  assert.cpp  error.cpp  sleep.cpp
src/platform/windows/      time_source.cpp  thread.cpp  (QueryPerformanceCounter, waitable timer, affinity, ...)
src/platform/posix/        time_source.cpp  thread.cpp  (clock_gettime, nanosleep, pthread_setaffinity_np, ...)

Note: `Clock` moved from platform to core during M1.2 (layering); see the ADR-007 amendment.
tests/unit/                strong_type_test.cpp  result_test.cpp  time_test.cpp  clock_test.cpp
                           thread_test.cpp  spsc_ring_test.cpp  spsc_ring_wrap_test.cpp
tests/stress/              spsc_ring_stress_test.cpp
tests/alloc/               spsc_ring_noalloc_test.cpp  (own binary: replaces operator new/delete)
benchmarks/                bm_spsc_ring.cpp  baseline_mutex_queue.hpp
docs/                      memory-ordering.md  PRD_spsc_ring.md (final)  adr/ADR-001, -007, -009 (accepted)
```

The ring is header-only (templates). Every hand-written file stays under the 150-code-line soft limit; the ring may be split into `spsc_ring_layout.hpp` + `spsc_ring.hpp` to stay there.

## Open questions for the owner (proposed answers in brackets)

1. **Default index type:** `std::uint64_t` [yes; wrap-around then never happens in practice, and tests use `uint8_t`/`uint16_t` to exercise it cheaply].
2. **Hot-path return types:** `bool` / pointer, not `Result` [yes; see ADR-001. `Result` is used for construction and setup only].
3. **Cache-line constants:** 64-byte alignment, 128-byte spacing between fields written by different threads [yes; see ADR-009].
4. **Cached indices live in the shared layout** (each on its own spacing block), not in per-side view objects [yes for M1; revisit in M2 if per-process views make it cleaner. Recorded in PRD §9].
