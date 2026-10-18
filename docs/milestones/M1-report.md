# M1 — Platform layer + lock-free SPSC ring: milestone report

- **Status:** **done (2026-10-18).** All steps green in CI on Windows and Linux (owner-confirmed; the owner moved on to M2).
- **Plan:** [M1-plan.md](M1-plan.md) · **PRD:** [PRD_spsc_ring.md](../PRD_spsc_ring.md) · **Ordering proof:** [memory-ordering.md](../memory-ordering.md)

## Summary

fastbus has its first real component: a bounded, lock-free, wait-free single-producer/single-consumer ring that works on caller-provided memory and holds no pointers inside it, so M2 can place the same bytes in shared memory unchanged. Under it sit the M1 foundations: strong ID types, `Result<T, E>` without exceptions, `Timestamp`/`Duration`, an injectable `Clock` with a deterministic `ManualClock`, precise sleeping, and thread placement and naming on Windows and Linux.

The ring's correctness argument was written **before** the code, with one tag per atomic operation; the code carries the same tags and was checked against the document's checklist. Then the tests were checked against the code: three deliberately broken versions of the ring were each caught (below).

## Acceptance criteria

| Criterion (TODO M1) | Status | Evidence |
|---|---|---|
| Ring passes unit, edge-case and stress tests on MSVC/GCC/Clang; ASan/UBSan/TSan clean | ✅ | 25 ring unit + wrap tests, 18 stress cases, 4 allocation tests. Linux GCC/Clang/ASan/UBSan/TSan/Release local runs; Windows MSVC Debug/Release, clang-cl, MSVC ASan and clang-tidy in CI |
| `memory-ordering.md` gives a complete happens-before argument for every atomic operation | ✅ | §4 lists all 10 operations; §5 proves the two synchronizes-with edges, §6 justifies each order, §7–§9 cover caching, `size_approx`, wrap; approved by the owner |
| Coverage gate ≥ 85 % active in CI for platform + core | ✅ | `linux-coverage` preset, `FASTBUS_COVERAGE_FAIL_UNDER=85`; currently **86.7 %** lines (781/901), 89.9 % functions; gate shown to fail at 99 % |
| No heap allocation inside push/pop, verified by test | ✅ | `fastbus_alloc_spsc_ring`: 0 allocations in 100 000 single-thread operations (all APIs) and in a 200 000-item two-thread transfer; `create` allocates exactly once |

## The tests were tested (mutation checks)

| Deliberate bug in the ring | Caught by | Result |
|---|---|---|
| Publish (`commit`) before writing the slot | stress test, Release | 38 312 out-of-order + 2 482 torn items reported |
| `[P3]` `tail.store` weakened from `release` to `relaxed` | TSan on the stress test | data race on the slot `memcpy` (invisible on x86 without TSan) |
| Hidden `new`/`delete` in `try_push` | allocation test | 50 000 / 210 797 allocations reported |

## Performance (indicative; M9 does the real methodology)

2-vCPU virtual machine, GCC 13 `-O3`, Google Benchmark, median of 3, threads pinned to CPU 0/1. Details in [benchmarks.md](../benchmarks.md).

| Metric | PRD hypothesis | Measured |
|---|---|---|
| Single-thread push + pop, 8-byte `T` | < 5 ns | **2.0 ns** (mutex + deque: 16.2 ns) |
| Two threads, 8-byte `T` | > 50 M items/s | **279 M items/s** |
| vs. `std::mutex + std::deque` (two threads) | > 5× | **114×** (2.4 M items/s) |
| Two threads, 64-byte `T` | — | 21.8 M items/s (1.3 GiB/s); baseline 2.8 M items/s |

## Delivered

- **platform:** `time_source` (QPC / `clock_gettime`, waitable high-resolution timer, `HighResolutionTimerScope`), `cpu_relax`, `thread` (pin/unpin, name, id, current CPU, concurrency). Windows and POSIX backends.
- **core:** `StrongType` + ID types, `Error`/`ErrorCode`, `Result<T, E>`, `fail_fast`/`check`/`debug_check`, `Timestamp`/`Duration`, `Clock` family, `sleep_for`/`precise_sleep_until`, `cache_line.hpp`, `SpscRingLayout`, `SpscRingView`, `SpscRing`, `ThreadOwner`.
- **tests:** unit (`tests/unit`), stress (`tests/stress`), allocation (`tests/alloc`); **benchmarks:** `bm_spsc_ring` + `baseline_mutex_queue.hpp` (kept for M9).
- **docs:** PRD (updated to what was built), ADR-001/007/009 accepted, `memory-ordering.md`, testing-strategy sections for stress and coverage, KI-005/008/009.
- **tooling:** coverage gate with `FASTBUS_COVERAGE_PATHS`, `-fprofile-update=atomic`, `dev.ps1 bench` runs every benchmark, CI runs the ring benchmark at smoke length.

## Bugs hit and fixed (all in [bug-log.md](../bug-log.md))

| ID | One line |
|---|---|
| BL-006 | gcov counters are not atomic by default → multi-threaded tests produced negative hit counts → `-fprofile-update=atomic` |
| BL-007 | Unquoted `: ` in a YAML job name made GitHub reject the workflow → quoted; `ci.yml` now checked with a YAML parser + actionlint |
| BL-008 | `fastbus_add_test` silently dropped every label after the first (gtest discovery flattens properties) → one label per binary, enforced with `FATAL_ERROR` |

Windows-only clang-tidy 19 findings were fixed as they appeared and are covered by KI-008: designated initializers, `kName` for static constants, void-cast of `timeEndPeriod` (M1.1/M1.2), and in M1.9 the allocation test's `operator delete` parameter names differing from MSVC's `_Block` (check disabled for `tests/alloc` only) plus `throw` rejected because the Windows tidy run treats exceptions as disabled (the test's `operator new` now aborts on exhaustion).

## Deviations from the plan

- `Clock` moved from platform to core (ADR-007 amendment) so platform stays free of core types.
- The ring view lives in `spsc_ring_view.hpp`; `spsc_ring.hpp` adds the owning `SpscRing` (file-length limit).
- `create(memory, capacity, start)` gained a `start` counter value for wrap tests and future M2 resumption (PRD updated).
- The allocation test does not run under TSan or MSVC ASan, whose runtimes own `operator new` (KI-009).
- TODO's `kCacheLine` is `kCacheLineSize` + `kFalseSharingRange` (ADR-009 names).

## Known limitations

- KI-005: TSan cannot see cross-process races; M2 adds cross-process invariant tests.
- KI-009: allocation test excluded under TSan and MSVC ASan.
- Debug misuse checks bind per view object; two separate copies of a view each accept their own threads.
- False sharing is prevented by layout (offsets asserted at compile time and tested), but not yet *measured*; the 64/128/no-padding ablation is scheduled for M9 (ADR-009).
- Benchmarks above come from a shared 2-vCPU VM; they support the hypotheses but are not publishable numbers.

## Explain-it-in-an-interview list

1. The two synchronizes-with edges ([P3]→[C2], [C3]→[P2]) and why everything else can be `relaxed`.
2. Why every [P3] must be `release` under the C++20 release-sequence rules (P0982).
3. Why `size_approx` loads `head` (acquire) before `tail`, and why it still needs a clamp.
4. Cached indices: why a stale cache is safe (it is never ahead of the real index).
5. The wrap rule: capacity ≤ 2^(n−1), demonstrated with 8- and 16-bit counters.
6. How a test was shown to catch a bug (mutation checks) and why TSan was needed for the `relaxed` mutation.
7. Why the allocation test is a separate binary, and why it cannot run under TSan.
8. Why gcov needs `-fprofile-update=atomic` for multi-threaded tests.

## Interview Q&As

Appended to [interview-prep.md](../interview-prep.md) (M1 section): acquire/release vs seq_cst, false sharing, monotonic indices and the overflow rule, plus why SPSC has no ABA problem.
