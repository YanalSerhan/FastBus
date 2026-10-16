# ADR-001: Error handling without exceptions on the hot path

- **Status:** proposed — awaiting owner approval (M1.0)
- **Date:** 2026-10-16
- **Deciders:** Yanal Serhan

## Context

The hot path (publish, loan, take, poll, callback dispatch) must not throw: exceptions have unpredictable cost, are banned in `core`/`transport` (TODO §3.2), and those modules are compiled with `-fno-exceptions` on GCC/Clang from M3. Setup code (creating a ring, opening shared memory) still has real failure modes that callers must handle. C++20 has no `std::expected` (added in C++23).

## Options

1. **Own `Result<T, E>`**: a small class template with `[[nodiscard]]`. No dependency, full control over size and codegen.
2. **Pinned `tl::expected`**: mature, API-compatible with `std::expected`, but an extra dependency in the lowest layer.
3. **Error codes + out-parameters**: no type, but easy to ignore, and poor ergonomics.
4. **Exceptions in setup only**: forces exception support on in `core`, which conflicts with `-fno-exceptions`.

## Decision

Option 1: own `fastbus::core::Result<T, E = Error>`.

- Its API is a strict **subset of `std::expected`**: `has_value()`, `explicit operator bool`, `value()`, `error()`, `operator*`, `operator->`, `value_or()`. Migrating to C++23 is then a type alias plus search-and-replace.
- `value()` on an error **asserts in debug builds and terminates** (`std::abort`) in release builds. It never throws.
- `Error` is a trivially copyable struct: `{ ErrorCode code; const char* message; }`. `ErrorCode` is an `enum class : std::uint16_t`, and `message` points to a static string literal. No allocation, so `Error` can be logged from any context.
- `Result<void>` is supported for operations that only succeed or fail.
- Rule: **construction and setup return `Result`**, through static factory functions instead of throwing constructors. **Hot-path operations return `bool` or a pointer** (full/empty is a normal outcome, not an error).

## Consequences

- ✅ No dependency, works under `-fno-exceptions`, `[[nodiscard]]` makes ignored errors a warning, and `-Werror` makes that an error.
- ✅ `std::expected` migration is mechanical.
- ❌ No monadic helpers (`and_then`, `transform`) at first. They are added only if real code needs them.
- ❌ One more type to test (M1.1: value and error paths, `Result<void>`, trivially copyable when `T` is, `static_assert` on size).
