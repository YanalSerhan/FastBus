# scripts/

Repository tooling. Python scripts always run through **uv** (`uv run python scripts/<name>.py`), which provides the pinned tools from the root `pyproject.toml` / `uv.lock`. The scripts themselves only use the standard library.

| Script | Purpose | Used by |
|---|---|---|
| `dev.ps1` | Windows developer entry point (doctor, build, test, asan, tidy, format, lint, bench, check) | you, CI Windows jobs |
| `run_clang_format.py --check / --fix` | clang-format 18 over all first-party C++ | CMake `format`/`format-check`, CI, pre-commit, `dev.ps1` |
| `check_file_length.py` | code-line limits for `include/` + `src/` (warn > 150, fail > 300) | CI `lint` job, `dev.ps1 lint` |
| `check_layering.py` | include-graph rules between modules | CI `lint` job, `dev.ps1 lint` |

Analysis/plotting scripts (M7, M9) will add their libraries as a separate dependency group in `pyproject.toml`.
