# `platform` module

**Status:** placeholder — implemented in M1 (clocks, threads), M2 (shared memory, processes), M6 (sockets).

**Responsibility.** Thin OS abstraction. The only module allowed to include OS headers (`<windows.h>`, `<sys/mman.h>`, ...). Backends live in `windows/` and `posix/`; exactly one is compiled (ADR-002).

**Public surface.** `Clock` + `SystemMonotonicClock`/`WallClock`/`ManualClock`, thread utilities (`cpu_relax`, affinity, names), `SharedMemory`, `Process`, `UdpSocket`, CRC32C hardware dispatch. Public headers live in `include/fastbus/platform/`; private headers sit next to the sources in `src/platform/`.

**Dependencies.** none (system libraries only). Enforced by `scripts/check_layering.py`.

**Building-block contract**

| | |
|---|---|
| Input data | OS handles, names, sizes, endpoints. |
| Output data | RAII wrappers returning `Result<T, Error>`; timestamps as strong types. |
| Setup data | none beyond construction arguments; timer-resolution tweaks are explicit opt-ins. |
