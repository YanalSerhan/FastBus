# fastbus — Product Requirements Document

> Status: **draft (G0.3)** — owner review pending. Numbers marked *hypothesis* are replaced by measured values in M9.
> Companion documents: [PLAN.md](PLAN.md) (design), [TODO.md](TODO.md) (tasks), [design-decisions.md](design-decisions.md) (ADRs).

## 1. Purpose

fastbus is a portfolio-grade, low-latency publish/subscribe middleware written in C++20. It exists to demonstrate — with code, tests, measurements and written explanations — systems-programming skills that are hard to show otherwise: lock-free concurrency and memory ordering, shared-memory IPC, binary layout/ABI discipline, code generation, deterministic record/replay, reliable networking under faults, and an applied estimation module (EKF).

## 2. Target users

| User | What they need from fastbus |
|---|---|
| Recruiters / hiring managers | A README that explains the project in 30 s, green CI, real numbers, real bugs found. |
| Interviewers (systems/robotics/HFT) | Code and docs they can drill into: memory-ordering proofs, ADRs, trade-offs, test strategy. |
| The owner | A codebase they can explain line by line (interview prep in every milestone). |
| A developer trying it | A 5-minute quickstart on Windows or Linux. |

## 3. Problem statement

Processes on one machine (and across a LAN) need to exchange small, typed messages with microsecond latency and no data races; when something goes wrong, the failure must be reproducible. Existing middleware solves this but is too large to study end-to-end. fastbus builds the essential mechanisms from scratch, at a size one person can understand, test and measure.

## 4. Goals

1. Zero-copy shared-memory pub/sub between processes on Windows and Linux.
2. Lock-free SPSC and MPSC queues with a documented happens-before argument.
3. Typed API (`Node`, `Publisher<T>`, `Subscriber<T>`) with QoS and discovery.
4. IDL + generator producing layout-checked, versioned message types.
5. Record/replay with byte-identical deterministic (lock-step) replay.
6. UDP transport with reliable QoS and seeded, cross-compiler-reproducible fault injection.
7. EKF module as a separate, versioned component consuming only the public API.
8. ≥ 2 genuine bugs found with these tools and written up.
9. Reproducible benchmarks with methodology, raw data and CI regression checks.

## 5. Non-goals

macOS; big-endian targets; dynamic-size message types (bounded `string<N>` only); security hardening against hostile local processes; full DDS/RTPS interoperability; multicast discovery (stretch only); production support.

## 6. KPIs (hypotheses until M9)

| KPI | Hypothesis | Measured in |
|---|---|---|
| Cross-process latency, 64 B, shm, p50 / p99 | < 1 µs / < 5 µs | M9 |
| Throughput, 64 B, shm, 1→1 | > 10 M msgs/s | M9 |
| Throughput at 1 MB payload (loan) | memory-bandwidth bound, size-independent per-message cost | M9 |
| Speed-up vs mutex+deque baseline | > 5× | M9 |
| Reliable UDP under 30 % loss | exactly-once, in-order | M6 |
| Replay determinism | 100 / 100 byte-identical runs | M5 |
| Line coverage of new code | ≥ 85 % | from M1 |

## 7. Acceptance criteria (project level)

See TODO.md §1 (global Definition of Done), each milestone's acceptance criteria, and §4 (final deliverables).

## 8. Constraints

C++20; CMake ≥ 3.24; MSVC 2022, GCC, Clang; Windows 10+, Ubuntu 22.04+; hot path free of allocation, exceptions, locks and syscalls; dependencies limited to the pinned set in [versions.md](versions.md).

## 9. Risks

See TODO.md Appendix B (R1–R10).
