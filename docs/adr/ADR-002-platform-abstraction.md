# ADR-002: Platform abstraction by build-time source selection

- **Status:** proposed (stub) — decided at the start of M2
- **Date:** —
- **Deciders:** Yanal Serhan

## Context

Windows and POSIX APIs for shm, processes, sockets and clocks differ; `#ifdef` in core logic hurts readability and testability.

## Options

(a) build-time selection of `src/platform/windows` vs `src/platform/posix` (scaffolded in `cmake/Platform.cmake`); (b) `#ifdef` inside shared files; (c) runtime polymorphism.

## Decision

_To be decided._

## Consequences

_To be written with the decision (positive, negative, follow-ups)._
