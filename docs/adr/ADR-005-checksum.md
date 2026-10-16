# ADR-005: Checksum algorithm and policy

- **Status:** proposed (stub) — decided at the start of M2
- **Date:** —
- **Deciders:** Yanal Serhan

## Context

Integrity checks for UDP, logs and optionally shm.

## Options

CRC32C (software table + SSE4.2/ARM CRC) vs xxHash vs none; mandatory on UDP/log, optional on shm.

## Decision

_To be decided._

## Consequences

_To be written with the decision (positive, negative, follow-ups)._
