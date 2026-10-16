# Changelog

All notable changes to this project are documented here.
Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versioning: [SemVer](https://semver.org/).

## [Unreleased]

### Added
- Repository scaffold (M0): CMake ≥ 3.24 project with presets for Windows (MSVC, clang-cl, ASan) and Linux (GCC, Clang, ASan, UBSan, TSan, coverage, clang-tidy, bench).
- `cmake/` modules: per-target strict warnings, sanitizers with combination checks, platform backend selection, pinned FetchContent dependencies, coverage (gcovr), clang-tidy, format targets.
- `fastbus::version()` (constexpr, generated header) and `fastbus::core::library_version()`.
- GoogleTest unit test, Google Benchmark smoke benchmark, sanitizer canaries.
- `.clang-format`, `.clang-tidy`, `scripts/` (format, file-length, layering checks), GitHub Actions CI, Dependabot.
- Planning docs: PRD, PLAN, TODO, ADR stubs (001–011), per-mechanism PRD stubs.
