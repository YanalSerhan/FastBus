# ADR-009: Cache-line size constant

- **Status:** proposed (stub) — decided at the start of M1
- **Date:** —
- **Deciders:** Yanal Serhan

## Context

False-sharing prevention needs a cache-line size.

## Options

Hard-coded 64 (128 on some ARM) vs `std::hardware_destructive_interference_size` (GCC `-Winterference-size` ABI warning).

## Decision

_To be decided._

## Consequences

_To be written with the decision (positive, negative, follow-ups)._
