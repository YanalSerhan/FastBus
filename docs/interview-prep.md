# Interview prep

Three Q&As appended at the end of every milestone.

## M0 — Repo scaffold

**Q1. Why CMake presets and per-target warning flags instead of global flags?**
Presets make every configuration (compiler, sanitizer, coverage) a named, reproducible one-liner shared by developers and CI. Warning flags live on an INTERFACE target linked PRIVATE to our own targets, so third-party code is not compiled with `-Werror` and a dependency update can't break the build, while every first-party target is guaranteed the same strict set.

**Q2. How do you keep platform code out of core logic?**
The build selects exactly one backend directory (`src/platform/windows` or `src/platform/posix`) at configure time; only the platform module includes OS headers, and `scripts/check_layering.py` fails CI if a higher layer reaches around it. Core code sees only portable interfaces, so there is no `#ifdef _WIN32` in the logic.

**Q3. How do you know your sanitizer job is really checking something?**
Each sanitizer preset builds a canary program with one deliberate bug (heap overflow, signed overflow, data race). The ctest entry passes only if the sanitizer's own report text appears in the output, so a misconfigured flag or preset turns the canary red instead of silently passing.
