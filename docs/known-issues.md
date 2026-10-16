# Known issues and limitations

| ID | Issue | Impact | Workaround | Planned fix |
|---|---|---|---|---|
| KI-001 | Windows build (MSVC, clang-cl) not yet verified by a CI run — M0 was verified locally on Linux only (GCC 13, Clang 18). | A Windows-only warning or config error could surface on the first CI run. | Run `cmake --preset windows-msvc` locally. | Close once the `windows-msvc` CI job is green. |
| KI-002 | clang-format output differs between major versions. | `format-check` may disagree between a local clang-format and CI. | Use clang-format **18** (`CLANG_FORMAT=clang-format-18`). | Revisit when CI moves off Ubuntu 24.04. |
| KI-003 | Sanitizers are Linux/Clang only in CI; MSVC ASan preset exists but is not in CI. | Windows-specific memory bugs are not caught automatically. | Run `windows-asan` locally. | P1, M0.4. |
| KI-004 | Files exempt from the file-length rule: tests, benchmarks, generated code, `src/platform/{windows,posix}`. | Those files can grow large. | Review. | Listed here as exemptions are added. |
| KI-005 | TSan cannot observe cross-process races. | Shm races between processes are invisible to TSan. | Ring logic validated in-process with threads (same code path) + cross-process invariant tests. | Documented again in M1.8. |
