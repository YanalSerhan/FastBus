# ADR-007: Injectable time

- **Status:** proposed (stub) — decided at the start of M1
- **Date:** —
- **Deciders:** Yanal Serhan

## Context

Deterministic tests, simulation and replay need control over time.

## Options

Injectable `Clock` interface (modules never call `steady_clock::now()` directly) vs global time hooks.

## Decision

_To be decided._

## Consequences

_To be written with the decision (positive, negative, follow-ups)._
