# `faults` module

**Status:** placeholder — implemented in M6.

**Responsibility.** `FaultyTransport<T>` decorator: drop, delay, duplicate, reorder, corrupt, bandwidth limit — seeded and reproducible (own PRNG, ADR-006).

**Public surface.** `fastbus/faults/*.hpp`. Public headers live in `include/fastbus/faults/`; private headers sit next to the sources in `src/faults/`.

**Dependencies.** `transport`, `core`, `platform`. Enforced by `scripts/check_layering.py`.

**Building-block contract**

| | |
|---|---|
| Input data | Packets from the wrapped transport. |
| Output data | Packets after faults + fault trace + counters. |
| Setup data | Seed + scenario JSON (`config/fault_scenarios/`). |
