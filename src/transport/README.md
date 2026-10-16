# `transport` module

**Status:** placeholder — implemented in M2 (shared memory), M6 (UDP).

**Responsibility.** Moves messages between endpoints: shared-memory segments with loans (zero-copy), UDP with fragmentation and reliability. Implements the internal `Transport` concept (ADR-008).

**Public surface.** Internal: `ShmTransport`, `UdpTransport`, `Loan<T>`, `Sample<T>`. Public headers live in `include/fastbus/transport/`; private headers sit next to the sources in `src/transport/`.

**Dependencies.** `core`, `platform`. Enforced by `scripts/check_layering.py`.

**Building-block contract**

| | |
|---|---|
| Input data | Message views / loans. |
| Output data | Delivered samples, per-endpoint stats. |
| Setup data | Segment names, sizes, QoS depth, peer list (JSON). |
