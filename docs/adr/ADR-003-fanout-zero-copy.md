# ADR-003: Fan-out / zero-copy design

- **Status:** proposed (stub) — decided at the start of M2
- **Date:** —
- **Deciders:** Yanal Serhan

## Context

One publisher, N subscribers; must stay zero-copy and allocation-free (open question Q2).

## Options

(a) per-subscriber descriptor rings into a publisher-owned shared slab with refcounts; (b) per-subscriber rings carrying full messages.

## Decision

_To be decided._

## Consequences

_To be written with the decision (positive, negative, follow-ups)._
