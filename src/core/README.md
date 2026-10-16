# `core` module

**Status:** placeholder — implemented in M1 (strong types, Result, SPSC ring), M2 (MPSC, CRC32C, slab).

**Responsibility.** Platform-independent building blocks: strong types, `Result<T,E>`, lock-free SPSC/MPSC rings as views over caller-provided memory, slab allocator, message header, CRC32C.

**Public surface.** `fastbus/core/*.hpp`. Public headers live in `include/fastbus/core/`; private headers sit next to the sources in `src/core/`.

**Dependencies.** `platform`. Enforced by `scripts/check_layering.py`.

**Building-block contract**

| | |
|---|---|
| Input data | Trivially-copyable elements; caller-provided memory regions. |
| Output data | Queue/ring operations (`try_push`, `try_pop`, claim/commit, peek/release). |
| Setup data | Capacity (power of two), memory region; nothing global. |
