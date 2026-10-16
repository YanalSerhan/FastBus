# FastBus

[![CI](https://github.com/YanalSerhan/FastBus/actions/workflows/ci.yml/badge.svg)](https://github.com/YanalSerhan/FastBus/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

**fastbus** is a low-latency publish/subscribe middleware in C++20: zero-copy shared-memory transport, lock-free SPSC/MPSC rings, an IDL code generator, deterministic record/replay, a reliable UDP transport with seeded fault injection, and an EKF sensor-fusion module built on top of it — all tested on Windows (MSVC) and Linux (GCC, Clang) with sanitizers in CI.

## What / Why

Robotics and trading-style systems need to move small messages between processes in microseconds, and to *reproduce* the bugs that happen when they do. fastbus is a from-scratch, fully tested implementation of the core ideas behind middleware such as iceoryx, DDS and ROS 2 bags, built to be explained line by line. Scope and goals: [`docs/PRD.md`](docs/PRD.md).

## Status

| Milestone | Scope | Status |
|---|---|---|
| G0 | Decisions, planning docs, ADR/PRD stubs | in progress |
| M0 | Repo scaffold: CMake + presets, CI, sanitizers, lint | in progress |
| M1 | Platform layer + lock-free SPSC ring | not started |
| M2 | Shared-memory transport, loans, MPSC | not started |
| M3 | Typed pub/sub API, discovery, QoS | not started |
| M4 | IDL + `fbgen` | not started |
| M5 | Record/replay | not started |
| M6 | UDP transport + fault injection | not started |
| M7 | EKF module | not started |
| M8 | Bug hunt | not started |
| M9 | Performance | not started |
| M10 | Docs and polish | not started |

Detailed tracker: [`docs/TODO.md`](docs/TODO.md).

## Architecture

_Placeholder — filled in M10._ Module layering and C4 diagrams: [`docs/PLAN.md`](docs/PLAN.md).

## Quickstart

Requirements: CMake ≥ 3.24, a C++20 compiler (MSVC 2022 / GCC ≥ 12 / Clang ≥ 16), Ninja (Linux), Python 3 (lint scripts), Git.

```bash
# Linux
cmake --preset linux-gcc
cmake --build --preset linux-gcc
ctest --preset linux-gcc
```

```powershell
# Windows (Developer PowerShell or any shell with CMake on PATH)
cmake --preset windows-msvc
cmake --build --preset windows-msvc
ctest --preset windows-msvc
```

`cmake --list-presets` shows every preset available on your OS. More in [CONTRIBUTING.md](CONTRIBUTING.md).

## Benchmarks

_Placeholder — measured numbers arrive in M9 ([`docs/benchmarks.md`](docs/benchmarks.md))._

## Bugs found with record/replay

_Placeholder — real write-ups arrive in M8 ([`docs/bugs/`](docs/bugs/))._

## Repository layout

```
include/fastbus/<module>/   public headers (platform, core, transport, api, idl, record, faults)
src/<module>/               implementation + private headers; src/platform/{windows,posix}
tools/                      fbgen, fbtop, fbrecord, fbplay, fbbench, fbinfo, fbclean
modules/ekf/                EKF sensor-fusion component (own version, public API only)
tests/                      unit, integration, stress, chaos, golden, canary, helpers
benchmarks/                 Google Benchmark suites
examples/                   runnable examples
idl/                        shared .fbidl message definitions
config/                     versioned JSON: QoS profiles, fault scenarios, sim/bench params
cmake/                      Warnings, Sanitizers, Platform, Dependencies, Coverage, Tidy, ...
scripts/                    lint/format scripts (Python); analysis scripts later (uv)
docs/                       PRD, PLAN, TODO, ADRs, per-mechanism PRDs, formats, bugs
```

## License

[MIT](LICENSE) © 2026 Yanal Serhan
