# scripts/

Repository tooling. The M0 scripts use only the Python standard library (Python ≥ 3.9), so they run anywhere without setup. Analysis/plotting scripts added in M7/M9 will be managed with `uv` (`pyproject.toml` + `uv.lock`).

| Script | Purpose | Used by |
|---|---|---|
| `run_clang_format.py --check / --fix` | clang-format over all first-party C++ | CMake `format`/`format-check`, CI, pre-commit |
| `check_file_length.py` | code-line limits for `include/` + `src/` (warn > 150, fail > 300) | CI `lint` job |
| `check_layering.py` | include-graph rules between modules | CI `lint` job |
