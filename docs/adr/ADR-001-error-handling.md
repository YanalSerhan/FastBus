# ADR-001: Error handling without exceptions on the hot path

- **Status:** proposed (stub) — decided at the start of M1
- **Date:** —
- **Deciders:** Yanal Serhan

## Context

C++20 has no `std::expected`; the hot path must not throw.

## Options

(a) own `Result<T,E>` with `[[nodiscard]]`; (b) pinned `tl::expected`; (c) error codes + out-params.

## Decision

_To be decided._

## Consequences

_To be written with the decision (positive, negative, follow-ups)._
