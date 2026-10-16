# Known issues and limitations

| ID | Issue | Impact | Workaround | Planned fix |
|---|---|---|---|---|
| KI-001 | ~~Windows build not yet verified by CI.~~ **Closed 2026-10-16:** `windows-msvc` green in CI run #1. clang-cl, MSVC Release, MSVC ASan and Windows clang-tidy jobs added afterwards; verified by their first CI run. | — | — | — |
| KI-002 | clang-format output differs between major versions. | A different local clang-format could disagree with CI. | **Mitigated:** clang-format 18.1.8 is pinned in `uv.lock` and used everywhere via `uv run` (CI, CMake, `dev.ps1`, pre-commit). | Bump deliberately with `uv lock --upgrade-package clang-format` + reformat commit. |
| KI-003 | ThreadSanitizer, UBSan, GCC and coverage have no MSVC equivalent, so they cannot run on a Windows-only machine. | Data races / UB are only caught after pushing. | They run in CI (Linux runners) on every push/PR; optional WSL2 for local runs. MSVC ASan runs locally (`dev.ps1 asan`) and in CI. | Permanent platform limitation; mitigated by CI. |
| KI-004 | Files exempt from the file-length rule: tests, benchmarks, generated code, `src/platform/{windows,posix}`. | Those files can grow large. | Review. | Listed here as exemptions are added. |
| KI-006 | Building inside a OneDrive-synced folder. | Slow builds, `LNK1104` file-lock errors, MAX_PATH failures in deep `_deps` paths. | Clone to a short local path (e.g. `C:\dev\FastBus`), `git config --global core.longpaths true`. `dev.ps1 doctor` warns. | Documentation only. |
| KI-005 | TSan cannot observe cross-process races. | Shm races between processes are invisible to TSan. | Ring logic validated in-process with threads (same code path) + cross-process invariant tests. | Documented again in M1.8. |
