# ADR-004: Wire / shared-memory layout rules

- **Status:** proposed (stub) — decided at the start of M2
- **Date:** —
- **Deciders:** Yanal Serhan

## Context

Shared memory and wire formats are read by other processes and compilers.

## Options

Little-endian only (`static_assert`), offsets not pointers, explicit padding, `static_assert`ed sizes/offsets.

## Decision

_To be decided._

## Consequences

_To be written with the decision (positive, negative, follow-ups)._
