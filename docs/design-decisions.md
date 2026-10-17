# Design decisions

Architecture Decision Records (ADRs) live in [`adr/`](adr/). Each has Context / Options / Decision / Consequences. Smaller non-obvious decisions (memory ordering, false sharing, layout/ABI, SPSC vs MPSC) are logged in the table at the bottom with their trade-offs.

## ADR index

| ADR | Title | Status | Decided in |
|---|---|---|---|
| [ADR-001](adr/ADR-001-error-handling.md) | Error handling without exceptions on the hot path | accepted | M1 |
| [ADR-002](adr/ADR-002-platform-abstraction.md) | Platform abstraction by build-time source selection | proposed | M2 |
| [ADR-003](adr/ADR-003-fanout-zero-copy.md) | Fan-out / zero-copy design | proposed | M2 |
| [ADR-004](adr/ADR-004-wire-shm-layout.md) | Wire / shared-memory layout rules | proposed | M2 |
| [ADR-005](adr/ADR-005-checksum.md) | Checksum algorithm and policy | proposed | M2 |
| [ADR-006](adr/ADR-006-prng.md) | Reproducible PRNG and distributions | proposed | M6 |
| [ADR-007](adr/ADR-007-clock-injection.md) | Injectable time | accepted | M1 |
| [ADR-008](adr/ADR-008-transport-interface.md) | Transport interface: static vs dynamic polymorphism | proposed | M3 |
| [ADR-009](adr/ADR-009-cache-line.md) | Cache-line size constant | accepted | M1 |
| [ADR-010](adr/ADR-010-registry-concurrency.md) | Discovery registry concurrency | proposed | M3 |
| [ADR-011](adr/ADR-011-benchmark-methodology.md) | Benchmark methodology and regression policy | proposed | M9 |

## Gate 0 open questions (G0.1)

The scaffold was built with the **proposed defaults**. Status: **confirmed by the owner on 2026-10-16**.

| Q | Question | Decision |
|---|---|---|
| Q1 | Dependencies | Yes to GoogleTest, Google Benchmark, CLI11, nlohmann_json, Eigen — pinned by tag + SHA ([versions.md](versions.md)). Eigen pinned when M7 starts. |
| Q2 | Zero-copy fan-out | (a) descriptor rings + shared slab with refcounts; formal decision in ADR-003 at M2. |
| Q3 | File-size rule | Soft limit: CI warning > 150 code lines, fail > 300, for `src/` + `include/`; tests, benchmarks, generated code and platform backends exempt (`scripts/check_file_length.py`). |
| Q4 | Toolchain | MSVC (VS 2022 generator) primary on Windows; clang-cl + Ninja secondary; WSL2 for local Linux/sanitizers. Windows 10+, Ubuntu 22.04+. macOS out of scope. |
| Q5 | License | MIT, © 2026 Yanal Serhan. Public/private timing: owner's choice (not affected by the scaffold). |

## Decision log (small decisions)

| Date | Area | Decision | Trade-off / reason |
|---|---|---|---|
| 2026-10-16 | build | Warnings via an INTERFACE target linked PRIVATE to our targets only | Third-party code (FetchContent) is never compiled with `-Werror`, so a dependency bump cannot break our build. |
| 2026-10-16 | build | Sanitizer flags applied globally (incl. dependencies) | TSan needs every TU instrumented to avoid false positives; ASan runtime must be consistent. |
| 2026-10-16 | build | FetchContent deps pinned by commit SHA, include dirs marked SYSTEM manually | `FetchContent_Declare(SYSTEM)` needs CMake 3.25; minimum is 3.24. |
| 2026-10-16 | test | Sanitizer canaries pass on the sanitizer's report text (`PASS_REGULAR_EXPRESSION`) instead of bare `WILL_FAIL` | `WILL_FAIL` would also pass on an unrelated crash; matching the report proves the right sanitizer fired. |
| 2026-10-16 | style | clang-format base = Google, 4-space indent, 100 columns | Google style matches the naming scheme (`kConstants`, `member_`); 4 spaces / 100 cols for readability of templated code. |
| 2026-10-17 | platform | Windows `sleep_for_ns` uses a high-resolution waitable timer, not `Sleep()` | `Sleep()` rounds up to the 15.6 ms system tick unless `timeBeginPeriod` changes a machine-wide setting; the high-resolution timer (Windows 10 1803+) gives ~0.5 ms without side effects. `Sleep()` remains the fallback. |
| 2026-10-17 | core | `Clock` lives in core; platform exposes raw `int64_t` ns reads | Respects the layering (platform must not include core types); see the ADR-007 amendment. |
| 2026-10-17 | core | `ManualClock` uses acquire/release on its atomic | A thread that observes the advanced time also observes everything the advancing thread did before advancing it, so simulation steps are well ordered. |
| 2026-10-17 | core | `StrongType<T, Tag>` with explicit construction and defaulted `<=>`, no arithmetic | IDs are compared and hashed, never added; arithmetic lives only in purpose-built types (`Duration`, `Timestamp`, `next(SequenceNumber)`), so the type system rejects `Timestamp + Timestamp`. |
| 2026-10-17 | core | `Error` carries a `const char*` to a static literal, not a `std::string` | Errors stay trivially copyable and allocation-free, so they can be created on any path and placed in shared memory; dynamic context goes to logs, not into the error. |
| 2026-10-16 | tooling | All Python tooling via uv (`pyproject.toml` + `uv.lock`), no pip | One command (`uv run`) gives every machine and CI job the same Python and tool versions; clang-format 18.1.8 from PyPI removes the local-vs-CI formatting mismatch; no global Python installs. `package = false`: the file pins tools, it is not a Python package. |
| 2026-10-16 | platform | Windows is the primary local dev platform; Linux-only checks (GCC, TSan, UBSan, coverage) are delegated to CI | Owner develops on Windows only. CI runs both OSes on every push, so nothing goes unchecked; `scripts/dev.ps1` is the single Windows entry point and CI's Windows jobs call the same script. |
| 2026-10-16 | build | clang-cl gets its own warning branch (no `/external:*`) | clang-cl rejects `/external:anglebrackets` under `/WX`; SYSTEM includes reach it as `-imsvc`, which already silences third-party headers. |
| 2026-10-16 | build | All executables go to `build/<preset>/bin[/<Config>]`; MSVC ASan runtime DLL is copied there | Without it, ASan tests fail with `0xc0000135` outside a Developer Prompt (and during gtest discovery). |
| 2026-10-16 | build | `version()` is constexpr in a generated header; `core::library_version()` is compiled | Lets tools detect header/library mismatch at runtime. |
| 2026-10-17 | core | Ring header stores the mask twice (producer block and consumer block) instead of a shared read-only config block | Keeps the header at 4 × 128 bytes as ADR-009 states, and lets each side's hot path touch only its own blocks. The view object (M1.5) holds no ring state, which is what M2 needs when each process has its own view. |
| 2026-10-17 | core | `size_approx` loads `head` with acquire, then `tail` relaxed, and clamps to capacity | Loading `head` first with acquire guarantees `tail ≥ head` (never a huge "negative" size); a third thread can still see more than `cap`, hence the clamp (memory-ordering.md §8). |
| 2026-10-17 | core | `create(memory, capacity, start = 0)`: both counters may start at any value | Lets tests start 8/16-bit counters just before the wrap point, so wrap-around is exercised in a few operations; M2 can reuse it to resume a ring. Not in the PRD sketch; PRD updated. |
| 2026-10-17 | core | Debug misuse checks live in the view (`ThreadOwner`, claim/peek flags), and the view is never written in release builds | Release builds keep zero overhead and no false sharing on the view object; a misuse check that relied on shared header state would change the shared-memory ABI. Limitation: two separate copies of a view each bind their own threads. |
| 2026-10-17 | core | `SpscRing` derives from `SpscRingView` and owns the memory | The owning ring has exactly the view's API with no forwarding boilerplate; `view()` hands out the view. |
| 2026-10-17 | tests | Stress item counts are scaled at compile time (`FASTBUS_STRESS_REDUCED`, `FASTBUS_STRESS_MULTIPLIER`), not via environment variables | `getenv` triggers MSVC C4996 (an error under `/WX`) and clang-tidy's `concurrency-mt-unsafe`; build-time scaling also makes each preset's behaviour explicit. |
| 2026-10-17 | tests | Coverage gate measures only the modules that are expected to meet it (`FASTBUS_COVERAGE_PATHS`, M1: core + platform) | Stub modules with no tests yet would drag the number down and make the gate meaningless; each milestone adds its module once it is tested. |
| 2026-10-17 | bench | The mutex + deque baseline is bounded to the ring's capacity and uses the same try_push/try_pop shape | A fair comparison: both queues refuse when full, so the difference is the synchronization, not unbounded buffering. |
| 2026-10-17 | tests | The allocation test counts only inside an armed window opened after setup, warm-up and thread creation | Setup allocations (the ring's one block, gtest, `std::thread`) are allowed by the PRD; anything counted inside the window comes from the ring operations. A control test proves the hook counts. |
