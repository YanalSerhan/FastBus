# Testing strategy

> Skeleton (M0). Completed in M10.

| Layer | Folder | Label | Catches | From |
|---|---|---|---|---|
| Unit | `tests/unit` | `unit` | logic errors, edge cases | M0 |
| Stress | `tests/stress` | `stress` | races, ordering bugs, torn payloads | M1 |
| Allocation | `tests/alloc` | `alloc` | heap allocations on hot paths (counting `operator new`, one binary per component) | M1 |
| Integration (cross-process) | `tests/integration` | `integration` | shm/process/socket behavior, crashes | M2 |
| Golden | `tests/golden` | `golden` | generator output drift | M4 |
| Chaos | `tests/chaos` | `chaos` | invariant violations under faults | M8 |
| Canary | `tests/canary` | `canary` | a sanitizer that silently stopped working | M0 |
| Sanitizers | CI jobs | — | UB, memory errors, data races | M0 |
| Coverage | CI job | — | untested code (gate ≥ 85 % from M1) | M0 |
| Benchmarks | `benchmarks/` | — | performance regressions | M0 smoke, M9 gate |

Rules: tests first or alongside; timing-dependent tests use virtual clocks; anything marked done was actually run.

## Cross-process tests (from M2.1)

- Child processes are always `tests/helpers/fb_test_child` (roles via argv), found through the `FASTBUS_TEST_CHILD` compile definition (`fastbus_use_test_child(<target>)`).
- No hang is possible: the helper has a watchdog (exits with 124, default 30 s), `platform::Process` kills its child on destruction, and every test has a ctest timeout.
- Shared-memory names come from `fastbus::test::unique_shm_name()` (tag + pid + counter), because ctest runs test cases as parallel processes.
- Assertions on failures use `fastbus::test::error_of(result)` instead of `.error().code`, so an unexpected success fails one assertion instead of aborting the binary in `Result::error()`.
- Behaviour that differs by OS on purpose (KI-010) is asserted per OS, so the difference cannot change silently.

## Stress tests (from M1.7)

- One test binary per component (`fastbus_stress_<name>`); each parameter combination is its own ctest entry with label `stress` and a 120 s timeout, plus an internal time limit so a slow machine reports partial progress instead of hanging.
- Payloads are self-checking: every word is derived from the sequence number, so the consumer can tell a lost/duplicated item (wrong sequence) from a torn one (right sequence, inconsistent words).
- Item counts: full in normal builds, 1/10 under sanitizers and coverage (`FASTBUS_STRESS_REDUCED`), scalable with `-DFASTBUS_STRESS_MULTIPLIER=N` for local soak runs.
- A stress test is only trusted after a **mutation check**: deliberately break the code under test and confirm the test (or TSan) fails. Results are recorded in TODO.md next to the test.
- Quick local loop: `ctest --preset <p> -L "unit|alloc"`; full: `ctest --preset <p>`. Each test binary has exactly one label (BL-008).

## Coverage gate (from M1.8)

- `linux-coverage` fails below `FASTBUS_COVERAGE_FAIL_UNDER` (85 %) line coverage over `FASTBUS_COVERAGE_PATHS` (M1: core + platform).
- Code that only runs in death-test children (`fail_fast`) is excluded with `GCOVR_EXCL_*` and a reason; nothing else is excluded.
- Coverage builds use `-fprofile-update=atomic` because the tests are multi-threaded (BL-006).
