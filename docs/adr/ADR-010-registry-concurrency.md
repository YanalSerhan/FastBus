# ADR-010: Discovery registry concurrency

- **Status:** proposed (stub) — decided at the start of M3
- **Date:** —
- **Deciders:** Yanal Serhan

## Context

Several processes register/unregister topics concurrently; processes may die at any time.

## Options

Lock-free slot claim (`free → claiming → ready` CAS) vs cross-process robust mutex.

## Decision

_To be decided._

## Consequences

_To be written with the decision (positive, negative, follow-ups)._
