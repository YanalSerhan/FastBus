# ADR-004: Wire / shared-memory layout rules

- **Status:** accepted (2026-10-18; owner delegated the decision to the proposed option)
- **Date:** 2026-10-18
- **Deciders:** Yanal Serhan

## Context

Shared-memory segments are read by processes built separately (possibly by different compilers: MSVC and clang-cl on Windows, GCC and Clang on Linux), and later the same structures go on the wire (M6) and into log files (M5). Layout must therefore be fixed by rules, not by whatever a compiler chooses. A crashed or buggy peer can also leave arbitrary bytes behind.

## Options

Rely on "same compiler, same flags" (fragile) vs explicit layout rules checked at compile time (chosen) vs a serialization format for everything (too slow for shm).

## Decision

Every structure placed in shared memory, on the wire or in a log file:
1. Uses fixed-width integers only (`std::uint32_t`, …); no `bool`, `enum` without explicit underlying type, `size_t`, `long`, or pointers.
2. Refers to other in-segment objects by **offset from the segment base** (`OffsetPtr<T>`), never by address.
3. Has explicit padding fields (named `reserved_*`) that are **always zeroed** on write; nothing relies on implicit padding.
4. `static_assert`s its `sizeof`, `alignof`, every field's `offsetof`, `is_trivially_copyable`, `is_standard_layout`.
5. Is **little-endian only**, enforced by `static_assert(std::endian::native == std::endian::little)`. Big-endian targets are out of scope.
6. Uses only atomics that are `is_always_lock_free` and the same size as the plain type (address-free across processes).
7. Starts (at segment, message or record level) with a magic number and a version; readers reject unknown versions.
8. Treats all of it as **untrusted on read**: every size, offset, index and count is bounds-checked before use, and failure is an error, never UB.
9. Uses ADR-009's constants for alignment and false-sharing separation.

## Consequences

- ✅ MSVC- and GCC-built processes agree byte for byte; mismatches fail the build, not the run.
- ✅ Corrupted segments produce clean errors (fuzz-tested in M2.8).
- ❌ More boilerplate (asserts, reserved fields, validation code) per structure.
- ❌ No big-endian support.
