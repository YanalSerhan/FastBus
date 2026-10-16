# Design decisions

Architecture Decision Records (ADRs) live in [`adr/`](adr/). Each has Context / Options / Decision / Consequences. Smaller non-obvious decisions (memory ordering, false sharing, layout/ABI, SPSC vs MPSC) are logged in the table at the bottom with their trade-offs.

## ADR index

| ADR | Title | Status | Decided in |
|---|---|---|---|
| [ADR-001](adr/ADR-001-error-handling.md) | Error handling without exceptions on the hot path | proposed | M1 |
| [ADR-002](adr/ADR-002-platform-abstraction.md) | Platform abstraction by build-time source selection | proposed | M2 |
| [ADR-003](adr/ADR-003-fanout-zero-copy.md) | Fan-out / zero-copy design | proposed | M2 |
| [ADR-004](adr/ADR-004-wire-shm-layout.md) | Wire / shared-memory layout rules | proposed | M2 |
| [ADR-005](adr/ADR-005-checksum.md) | Checksum algorithm and policy | proposed | M2 |
| [ADR-006](adr/ADR-006-prng.md) | Reproducible PRNG and distributions | proposed | M6 |
| [ADR-007](adr/ADR-007-clock-injection.md) | Injectable time | proposed | M1 |
| [ADR-008](adr/ADR-008-transport-interface.md) | Transport interface: static vs dynamic polymorphism | proposed | M3 |
| [ADR-009](adr/ADR-009-cache-line.md) | Cache-line size constant | proposed | M1 |
| [ADR-010](adr/ADR-010-registry-concurrency.md) | Discovery registry concurrency | proposed | M3 |
| [ADR-011](adr/ADR-011-benchmark-methodology.md) | Benchmark methodology and regression policy | proposed | M9 |

## Gate 0 open questions (G0.1)

The scaffold was built with the **proposed defaults**. Status: *adopted provisionally — awaiting owner confirmation*. Changing an answer later is cheap at this stage.

| Q | Question | Provisional answer |
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
| 2026-10-16 | build | `version()` is constexpr in a generated header; `core::library_version()` is compiled | Lets tools detect header/library mismatch at runtime. |
