# Changelog

All notable changes to this project are documented here.
Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versioning: [SemVer](https://semver.org/).

## [Unreleased]

### Added
- Windows-first workflow: `scripts/dev.ps1` (doctor, build, test, asan, tidy, format, lint, bench, check) that enters the VS developer environment automatically.
- `windows-tidy` preset; CI Windows jobs for MSVC Debug/Release, clang-cl, MSVC ASan and clang-tidy, all via `dev.ps1`.

### Fixed
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
