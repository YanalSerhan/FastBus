# `api` module

**Status:** placeholder — implemented in M3.

**Responsibility.** The public SDK: `Node`, `Publisher<T>`, `Subscriber<T>`, QoS, discovery, executor. The **only** surface tools, examples and modules may use.

**Public surface.** `fastbus/api/*.hpp`. Public headers live in `include/fastbus/api/`; private headers sit next to the sources in `src/api/`.

**Dependencies.** `transport`, `faults`, `idl`, `core`, `platform`. Enforced by `scripts/check_layering.py`.

**Building-block contract**

| | |
|---|---|
| Input data | User messages (loaned or copied). |
| Output data | Callbacks / polled samples, status events (QoS incompatibility, deadline missed). |
| Setup data | Node name, domain, QoS profiles (`config/qos_profiles.json`). |
