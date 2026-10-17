# Changelog

All notable changes to this project are documented here.
Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versioning: [SemVer](https://semver.org/).

## [Unreleased]

### Added
- Time (M1.2): `fastbus::core::Clock` with `SystemMonotonicClock` (QPC / `CLOCK_MONOTONIC`), `WallClock`, thread-safe `ManualClock`; `sleep_for` and `precise_sleep_until` (sleep, then spin); `platform::cpu_relax()`, `platform::HighResolutionTimerScope` (opt-in `timeBeginPeriod`). `check_layering.py` now rejects std clocks outside `src/platform/` and `benchmarks/` (ADR-007).
- Threads (M1.3): `fastbus::platform` `pin_current_thread` / `unpin_current_thread`, `set_current_thread_name` / `current_thread_name` (15-char ASCII, portable), `current_thread_id`, `current_cpu`, `hardware_concurrency` (Windows and POSIX backends).
- Ring layout (M1.4): `core/cache_line.hpp` (`kCacheLineSize` 64, `kFalseSharingRange` 128), `core/spsc_ring_layout.hpp` (`RingElement`, `RingIndex`, `SpscRingHeader`, `SpscRingLayout`, `ring_distance`/`ring_next`/`ring_slot`), and `docs/memory-ordering.md` (full happens-before argument, written before the ring code).
- SPSC ring (M1.5/M1.6): `core::SpscRingView` (over caller memory) and `core::SpscRing` (owning): `try_push`/`try_pop`, zero-copy `try_claim`+`commit` and `try_peek`+`release`, `size_approx`/`empty_approx`/`capacity`; debug-only misuse checks (`core::ThreadOwner`). Unit, edge-case and 8/16-bit counter wrap tests.
- Coverage builds use `-fprofile-update=atomic` (BL-006).
- `fastbus::core` (M1.1): `StrongType<T, Tag>` and the ID types (`TopicId`, `NodeId`, `PublisherId`, `SequenceNumber`, `TypeId`, `SchemaVersion`), `Duration`/`Timestamp`, `Error`/`ErrorCode`, `Result<T, E>` (std::expected subset), `fail_fast`/`check`/`debug_check` contract helpers, with unit and death tests.
- Windows-first workflow: `scripts/dev.ps1` (doctor, build, test, asan, tidy, format, lint, bench, check) that enters the VS developer environment automatically.
- `windows-tidy` preset; CI Windows jobs for MSVC Debug/Release, clang-cl, MSVC ASan and clang-tidy, all via `dev.ps1`.

- Python tooling managed by uv: `pyproject.toml`, `uv.lock` and `.python-version` pin clang-format 18.1.8, gcovr 8.6 and pre-commit. Scripts, CMake targets, pre-commit, `dev.ps1` and CI all run through `uv run`.

### Fixed
- `dev.ps1` exits explicitly, so a stale native exit code (e.g. from `git config --get`) no longer fails CI steps.
- clang-cl presets set the C compiler too (GoogleTest enables C); clang-tidy is found on PATH before fallback install folders.
- `windows-clangcl` preset: clang-cl rejected `/external:anglebrackets` under `/WX`; clang-cl now has its own warning flags.
- MSVC ASan tests failing with "DLL not found" outside a Developer Prompt: the ASan runtime is copied next to every executable.
- `run_clang_format.py`: finds clang-format from Visual Studio / LLVM on Windows, warns on version mismatch, batches files to stay under the Windows command-line limit.

### Changed
- Executables are written to `build/<preset>/bin/` (`bin/<Config>/` for Visual Studio presets).

### Added — initial scaffold (M0)
- Repository scaffold (M0): CMake ≥ 3.24 project with presets for Windows (MSVC, clang-cl, ASan) and Linux (GCC, Clang, ASan, UBSan, TSan, coverage, clang-tidy, bench).
- `cmake/` modules: per-target strict warnings, sanitizers with combination checks, platform backend selection, pinned FetchContent dependencies, coverage (gcovr), clang-tidy, format targets.
- `fastbus::version()` (constexpr, generated header) and `fastbus::core::library_version()`.
- GoogleTest unit test, Google Benchmark smoke benchmark, sanitizer canaries.
- `.clang-format`, `.clang-tidy`, `scripts/` (format, file-length, layering checks), GitHub Actions CI, Dependabot.
- Planning docs: PRD, PLAN, TODO, ADR stubs (001–011), per-mechanism PRD stubs.
