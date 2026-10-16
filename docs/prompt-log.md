# AI-assistance log

Honest record of AI assistance (standards doc §8.3). Every line must be explainable by the owner.

| Date | Intent | Prompt summary | Outcome |
|---|---|---|---|
| 2026-10-16 | Set up the project from TODO.md | "Read the TODO and set up the project (no commits)" | M0 scaffold + Gate 0 doc skeletons generated; verified locally on Linux (GCC/Clang, ASan/UBSan/TSan, tidy, coverage). Owner to review, commit and push. |
| 2026-10-16 | Windows-first + uv tooling, fix CI | "Make it work on Windows", "use uv not pip", CI failure logs | `dev.ps1`, clang-cl fixes, uv-pinned tools; 5 bugs found and logged (BL-001..005); CI fully green |
| 2026-10-16 | Close M0, plan M1 | "confirm" (G0.1) | M0 report; M1 plan, PRD_spsc_ring, ADR-001/007/009 drafted for approval |
