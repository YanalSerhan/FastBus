# ADR-002: Platform abstraction by build-time source selection

- **Status:** proposed (2026-10-18), implemented provisionally for M2.1/M2.2; awaiting owner approval
- **Date:** 2026-10-18
- **Deciders:** Yanal Serhan

## Context

Windows and POSIX APIs for shared memory, processes, sockets and clocks differ completely (`CreateFileMappingW` vs `shm_open`, `CreateProcessW` vs `posix_spawn`, …). `#ifdef` inside core logic hurts readability and testability. Since M0 the build has picked one backend directory (`cmake/Platform.cmake`), and M1 used it for clocks and threads.

M2 adds a second question: platform setup calls (create a segment, spawn a process) have real failure modes, and the TODO asks for `Result` errors. But `Result`/`Error`/`fail_fast` live in `core`, and the layering forbids `platform → core`. Linking `platform` against `core` would also create a static-link cycle (`core` already depends on `platform`).

## Options

**Backend selection:** (a) build-time selection of `src/platform/windows` or `src/platform/posix`; (b) `#ifdef` inside shared files; (c) runtime polymorphism (virtual interface + factory).

**Errors in platform:** (1) a second, platform-only result type; (2) out-parameters and native error codes, converted to `Result` in core; (3) split the dependency-free **vocabulary** (`result.hpp`, `error.hpp`, `assert.hpp` and their two `.cpp` files) into its own library `fastbus::vocabulary` below `platform`, which both `platform` and `core` use.

## Decision

- **(a) build-time selection.** Each platform header in `include/fastbus/platform/` has exactly one implementation per backend directory. Code shared by both backends (e.g. shared-memory name validation) lives in `src/platform/common/`. OS headers are included only in backend `.cpp` files, never in a public header.
- **(3) vocabulary layer.** `fastbus::vocabulary` = `core/assert.cpp` + `core/error.cpp`, with no dependencies. `platform` links it PUBLIC. `scripts/check_layering.py` lets platform include exactly `fastbus/core/{result,error,assert}.hpp` and nothing else from core. Files keep their paths and namespace (`fastbus::core`), so no M1 code changes.

## Consequences

- ✅ No `#ifdef _WIN32` in core logic; each backend is plain, readable OS code.
- ✅ One error vocabulary across the whole library; platform failures carry the same `ErrorCode`s as everything else.
- ✅ No link cycle: link order is core → platform → vocabulary.
- ❌ The layering rule now has a named exception, which the check script spells out explicitly.
- ❌ Build-time selection means one backend per build; no runtime switching (not needed).
- Follow-up: new `ErrorCode`s appended for M2 (`AlreadyExists`, `NotFound`, `PermissionDenied`, `Timeout`; values 8–11, append-only).
