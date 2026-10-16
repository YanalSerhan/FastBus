# Testing strategy

> Skeleton (M0). Completed in M10.

| Layer | Folder | Label | Catches | From |
|---|---|---|---|---|
| Unit | `tests/unit` | `unit` | logic errors, edge cases | M0 |
| Stress | `tests/stress` | `stress` | races, ordering bugs, torn payloads | M1 |
| Integration (cross-process) | `tests/integration` | `integration` | shm/process/socket behavior, crashes | M2 |
| Golden | `tests/golden` | `golden` | generator output drift | M4 |
| Chaos | `tests/chaos` | `chaos` | invariant violations under faults | M8 |
| Canary | `tests/canary` | `canary` | a sanitizer that silently stopped working | M0 |
| Sanitizers | CI jobs | — | UB, memory errors, data races | M0 |
| Coverage | CI job | — | untested code (gate ≥ 85 % from M1) | M0 |
| Benchmarks | `benchmarks/` | — | performance regressions | M0 smoke, M9 gate |

Rules: tests first or alongside; timing-dependent tests use virtual clocks; anything marked done was actually run.
