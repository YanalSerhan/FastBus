# ADR-006: Reproducible PRNG and distributions

- **Status:** proposed (stub) — decided at the start of M6
- **Date:** —
- **Deciders:** Yanal Serhan

## Context

`<random>` distributions are implementation-defined, so seeded runs differ across MSVC/libstdc++/libc++.

## Options

Own PCG32 / xoshiro256** + own distributions vs `<random>` engines with own distributions only.

## Decision

_To be decided._

## Consequences

_To be written with the decision (positive, negative, follow-ups)._
