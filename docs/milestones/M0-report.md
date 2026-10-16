# M0 — Repo scaffold: milestone report

- **Status:** done (2026-10-16)
- **Evidence:** [CI run 38907993718](https://github.com/YanalSerhan/FastBus/actions/runs/38907993718) on commit `926620d`, all jobs green; owner's Windows machine (VS 2022 17.14) green for MSVC Debug/Release and MSVC ASan.

## Summary

The repository builds, tests, lints and benchmarks with one command per preset on Windows (MSVC, clang-cl) and Linux (GCC, Clang). Every compiler runs with warnings-as-errors. ASan, UBSan, TSan and MSVC ASan each have a canary that proves the sanitizer is active, and CI runs all of it on every push. Windows is the primary dev platform: `scripts/dev.ps1` is the single entry point, and CI's Windows jobs call the same script. All Python tooling is pinned through uv.

Gate 0: Q1–Q5 confirmed by the owner. Planning docs (PRD, PLAN, TODO), ADR index with 11 stubs, and 10 per-mechanism PRD stubs are in place.

## What was verified

| Acceptance criterion (TODO M0) | Status | Evidence |
|---|---|---|
| Fresh clone builds + tests with one command per preset on Windows (MSVC) and Linux (gcc, clang) | ✅ | CI `windows (msvc-debug/-release)`, `build-test (linux-gcc/-clang)`; owner machine |
| CI green on all matrix jobs | ✅ | Run 38907993718: lint, 5 Windows jobs, 2 Linux build-test, 3 sanitizers, tidy, coverage, bench-smoke, markdown links |
| A deliberate warning breaks CI | ⚠️ partly | Verified locally: an unused variable fails GCC, Clang and clang-tidy builds. The CI scratch-branch proof is left to the owner. |
| TSan canary proves the sanitizer is active | ✅ | `canary_tsan_data_race` passes only when the TSan report text appears (same for ASan, UBSan, MSVC ASan) |
| Benchmark smoke run executes in CI | ✅ | `bench-smoke` job, JSON artifact |
| README, CONTRIBUTING, LICENSE, CHANGELOG, known-issues, design-decisions exist | ✅ | repo root + `docs/` |

## Bugs hit and fixed (all in [bug-log.md](../bug-log.md))

| ID | One line |
|---|---|
| BL-001 | clang-cl rejected the MSVC-only `/external:anglebrackets` under `/WX` → clang-cl got its own warning flags |
| BL-002 | `dev.ps1` leaked a stale `$LASTEXITCODE` from `git config --get` → every Windows CI job failed at "Toolchain check" |
| BL-003 | GoogleTest enables C; preset only set the C++ compiler → GNU-style `clang.exe` got MSVC flags |
| BL-004 | `find_program(HINTS ...)` searched hints before PATH → wrong clang-tidy |
| BL-005 | MSVC ASan on VS 17.4 → `0xc0000142`; fixed by updating VS (environment, not code) |

## Known limitations (see [known-issues.md](../known-issues.md))

- KI-003: TSan, UBSan, GCC and coverage cannot run on a Windows-only machine; they run in CI on every push.
- KI-006: the repo lives in OneDrive. Builds work, but OneDrive reverted files written into the folder twice during M0; changes are therefore delivered as zips.
- KI-007: VS 2022 17.4 breaks MSVC ASan; 17.10+ required.
- Coverage is report-only (gate 0 %); the 85 % gate switches on in M1.
- Branch protection on `main` is not enabled yet (owner action in GitHub settings).

## Explain-it-in-an-interview list

1. Why warnings live on an INTERFACE target linked PRIVATE (third-party code never sees `-Werror`).
2. Why sanitizer flags are global but warning flags are per-target (TSan needs everything instrumented).
3. Why canaries match the sanitizer's report text instead of using `WILL_FAIL`.
4. How the platform backend is chosen at build time with no `#ifdef` in core logic (ADR-002).
5. Why dependencies are pinned by commit SHA and their include directories marked SYSTEM by hand (CMake 3.24 has no `FetchContent SYSTEM`).
6. How BL-002 happened: PowerShell's `$LASTEXITCODE` and GitHub's `exit $LASTEXITCODE` wrapper.
7. How BL-003 happened: a CXX-only project still gets a C compiler when a dependency calls `project()` with C enabled.
8. Why clang-format is pinned through uv (PyPI wheel) instead of the system package (identical output on every OS).

## Interview Q&As

Appended to [interview-prep.md](../interview-prep.md) (M0 section): presets and per-target warnings, keeping platform code out of core logic, proving a sanitizer is active.
