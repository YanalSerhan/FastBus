# `record` module

**Status:** placeholder — implemented in M5.

**Responsibility.** `.fblog` recorder and player; deterministic lock-step replay.

**Public surface.** `Recorder`, `Player`, `.fblog` reader/writer (`docs/formats/fblog-v1.md`). Public headers live in `include/fastbus/record/`; private headers sit next to the sources in `src/record/`.

**Dependencies.** `transport`, `core`, `platform`. Enforced by `scripts/check_layering.py`.

**Building-block contract**

| | |
|---|---|
| Input data | Subscribed samples (recorder) / log file (player). |
| Output data | Log file (recorder) / republished samples (player). |
| Setup data | Topic filters, rate/mode, deterministic flag. |
