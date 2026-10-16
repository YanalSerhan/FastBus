# `idl` module

**Status:** placeholder — implemented in M4.

**Responsibility.** Runtime side of the IDL: `MessageTraits`, descriptors, schema compatibility adaptation used by generated code. The generator itself lives in `tools/fbgen`.

**Public surface.** `fastbus/idl/*.hpp`. Public headers live in `include/fastbus/idl/`; private headers sit next to the sources in `src/idl/`.

**Dependencies.** `core`, `platform`. Enforced by `scripts/check_layering.py`.

**Building-block contract**

| | |
|---|---|
| Input data | Payload bytes + header (schema version). |
| Output data | Typed, version-adapted views. |
| Setup data | Generated descriptor tables. |
