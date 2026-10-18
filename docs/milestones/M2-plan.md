# M2 — Shared-memory transport (2 processes), message header, loans, MPSC: plan

> **Status: proposed 2026-10-18.** M2.0 docs written; M2.1 (shared memory) and M2.2 (processes) implemented on the platform layer, which does not depend on the open ADR-003/004/005 choices. **Owner approval of [PRD_shm_transport.md](../PRD_shm_transport.md), [PRD_mpsc_queue.md](../PRD_mpsc_queue.md) and ADR-002/003/004/005 is needed before M2.3.**

## Scope

Platform shared memory and process utilities; a segment protocol with stale-creator detection; a 64-byte message header with CRC32C; a slab allocator in shared memory; loan-based zero-copy publish; an MPSC queue; cross-process integration tests on Windows and Linux; `fbclean`.

**Out of scope:** the typed pub/sub API, discovery and QoS (M3); IDL (M4); blocking wait strategies; UDP (M6).

## Acceptance criteria (from TODO.md)

1. Two separate processes exchange ≥ 1e6 messages with order and integrity verification on Windows and Linux.
2. The loan API is demonstrably zero-copy: the subscriber's pointer lies inside its own mapping, and nothing is memcpy'd on the path.
3. A crashed publisher never exposes a torn message; stale segments are detected and recovered.
4. MPSC passes multi-producer stress under TSan (in-process threads).

## Work order

| Step | TODO | Deliverable | Depends on |
|---|---|---|---|
| 0 | M2.0 | This plan, two PRDs, ADR-002/003/004/005, lifetime semantics | — |
| 1 | M2.1 | `platform::SharedMemory` (Windows + POSIX), name rules, tests incl. cross-process | ADR-002 |
| 2 | M2.2 | `platform::Process`, `ProcessIdentity` (pid + start time), `fb_test_child`, tests | ADR-002 |
| 3 | M2.3 | `SegmentHeader`, `OffsetPtr`, validation, stale detection | **approval**, ADR-004 |
| 4 | M2.4 | `MessageHeader`, CRC32C (software + hardware) | ADR-004, ADR-005 |
| 5 | M2.5 | Slab with refcounts + return rings | ADR-003 |
| 6 | M2.6 | MPSC queue (new ADR, PRD_mpsc option B proposed) | PRD_mpsc approval |
| 7 | M2.7 | `ShmTransport`, `Loan<T>`, `Sample<T>` | 3–5 |
| 8 | M2.8 | Cross-process integration suite (Windows + Linux CI) | 7 |
| 9 | M2.9 | `fbclean`, docs | 3 |
| 10 | M2.10 | Milestone report | all |

## Files (M2.0–M2.2)

```
include/fastbus/platform/  shared_memory.hpp  process.hpp
src/platform/common/       shared_memory_common.cpp  process_common.cpp   (shared by both backends)
src/platform/windows/      shared_memory.cpp  process.cpp
src/platform/posix/        shared_memory.cpp  process.cpp
include/fastbus/core/      error.hpp          (+ AlreadyExists, NotFound, PermissionDenied, Timeout)
src/core/CMakeLists.txt    fastbus::vocabulary target (ADR-002)
tests/helpers/             fb_test_child.cpp  (roles via argv, watchdog)
tests/unit/                shared_memory_test.cpp  process_test.cpp
```

## Risks

| Risk | Mitigation |
|---|---|
| Windows and POSIX lifetime semantics differ | "Creator owns lifetime" + `closing` state (PRD §3.1); tests assert the common behaviour only |
| PID reuse makes a dead creator look alive | Identity = pid + start time; dedicated test with a wrong start time |
| CI containers restrict `/dev/shm` size or process limits | Tests use small segments; failures report the OS error |
| A hung child process hangs CI | `fb_test_child` watchdog + `Process` destructor kills + ctest timeouts |
| Windows-only clang-tidy 19 findings (KI-008) | Windows OS code is written defensively (void casts, no macros with casts, named constants); run `dev.ps1 tidy` locally |

## Open questions for the owner

See PRD_shm_transport §13 and PRD_mpsc_queue §9: lifetime semantics, the vocabulary layering exception, ADR-003/004/005, and MPSC option (B).
