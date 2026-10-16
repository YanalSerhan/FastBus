# Contributing to fastbus

## Windows quick start (primary platform)

You need **Visual Studio 2022** with the *Desktop development with C++* workload, **Git** and **[uv](https://docs.astral.sh/uv/)** (`winget install astral-sh.uv`). Optional but recommended: the *C++ Clang Compiler for Windows* component (VS Installer → Modify → Individual components), which provides clang-cl and clang-tidy.

You do **not** need to install Python or any Python package yourself: uv downloads a matching Python and the pinned tools from `uv.lock` into `.venv` the first time a script runs.

Everything goes through one script, run from a normal PowerShell in the repo root. It enters the Visual Studio developer environment by itself when needed:

| Command | What it does |
|---|---|
| `.\scripts\dev.ps1 doctor` | Checks tools, CMake version, repo location; tells you how to fix what's missing |
| `.\scripts\dev.ps1` | Configure + build + test with MSVC (Debug) |
| `.\scripts\dev.ps1 all -Config Release` | Same, Release |
| `.\scripts\dev.ps1 all -Preset windows-clangcl` | Same with clang-cl (stricter warnings, catches non-portable code) |
| `.\scripts\dev.ps1 asan` | MSVC AddressSanitizer build + tests + ASan canary |
| `.\scripts\dev.ps1 tidy` | Build with clang-tidy on every file |
| `.\scripts\dev.ps1 format` / `lint` | Format sources / run the same lint checks as CI |
| `.\scripts\dev.ps1 bench` | Release build + benchmarks |
| `.\scripts\dev.ps1 check` | Everything CI runs on Windows: lint, Debug + Release, ASan, tidy |
| `.\scripts\dev.ps1 clean -Preset <p>` | Delete `build\<p>` |

If PowerShell refuses to run scripts, allow local scripts once: `Set-ExecutionPolicy -Scope CurrentUser RemoteSigned`.

**Keep the repo out of OneDrive.** OneDrive syncs and locks thousands of build files, which makes builds slow and causes random `LNK1104: cannot open file` errors. Use a short local path such as `C:\dev\FastBus`, and run `git config --global core.longpaths true`.

**What Windows cannot run locally** — GCC, ThreadSanitizer, UndefinedBehaviorSanitizer and the coverage report have no MSVC equivalent. They run in CI (GitHub's Linux runners) on every push and pull request; check the Actions tab. If you ever want them locally, WSL2 gives you a real Linux (see below), but it is optional.

Visual Studio users can also open the folder directly (*File → Open → Folder*): VS reads `CMakePresets.json` and offers the `windows-*` presets in its toolbar.

## Build (all platforms)

All builds go through CMake presets; each preset builds into `build/<preset>/`, executables into `build/<preset>/bin/` (`bin/<Config>/` for Visual Studio presets). In-source builds are refused.

| OS | Preset | Notes |
|---|---|---|
| Windows | `windows-msvc` | Visual Studio 2022 generator (primary). Build presets `windows-msvc` (Debug) and `windows-msvc-release`. |
| Windows | `windows-clangcl` | clang-cl + Ninja; `dev.ps1` enters the VS developer environment for you. Emits `compile_commands.json` for clangd / clang-tidy. |
| Windows | `windows-asan` | MSVC AddressSanitizer; the ASan runtime DLL is copied next to every exe, so tests run from any shell. |
| Windows | `windows-tidy` | clang-cl + clang-tidy on every TU during the build. |
| Linux | `linux-gcc`, `linux-clang` | Ninja, Debug. |
| Linux | `linux-asan`, `linux-ubsan`, `linux-tsan` | Clang + sanitizer; includes the canary for that sanitizer. |
| Linux | `linux-coverage` | GCC + `--coverage`; `cmake --build --preset linux-coverage --target coverage` (gcovr via uv). |
| Linux | `linux-tidy` | Clang with clang-tidy run on every TU during the build. |
| Linux | `linux-bench` | GCC Release with frame pointers, for benchmarks/profiling. |

```bash
cmake --preset <preset>
cmake --build --preset <preset>
ctest --preset <preset>
```

Local overrides: copy `CMakeUserPresets.json.example` to `CMakeUserPresets.json` (gitignored).

### Optional: Linux presets from Windows (WSL2)

```powershell
wsl --install -d Ubuntu-24.04          # once
```
```bash
# inside WSL — clone into the Linux filesystem (~/src), not /mnt/c, for speed and correct permissions
sudo apt update && sudo apt install -y build-essential clang clang-tidy-18 clang-format-18 \
     libclang-rt-18-dev cmake ninja-build git
curl -LsSf https://astral.sh/uv/install.sh | sh     # uv
git clone https://github.com/YanalSerhan/FastBus.git ~/src/FastBus && cd ~/src/FastBus
cmake --preset linux-clang && cmake --build --preset linux-clang && ctest --preset linux-clang
```

**TSan on WSL2 / recent kernels:** if TSan aborts with `unexpected memory mapping`, run `sudo sysctl vm.mmap_rnd_bits=28` (resets on reboot).

## Python tooling (uv)

All Python in this repo runs through **uv**; never `pip install` anything. `pyproject.toml` lists the tools, `uv.lock` pins their exact versions for every OS, and `.python-version` pins the interpreter.

| Task | Command |
|---|---|
| Create / refresh `.venv` | `uv sync` (also happens automatically on `uv run`) |
| Run a repo script | `uv run python scripts/check_layering.py` |
| Run a pinned tool | `uv run clang-format --version`, `uv run gcovr --version` |
| Add a tool | `uv add --dev <package>` → commit `pyproject.toml` + `uv.lock` |
| Upgrade one tool | `uv lock --upgrade-package gcovr` → commit `uv.lock` |

CI runs `uv sync --locked`, which fails if `uv.lock` is out of date with `pyproject.toml`, so always commit both together. CMake's `format`, `format-check` and `coverage` targets call uv too.

## Formatting and lint

- `.\scripts\dev.ps1 format` (Windows) or `uv run python scripts/run_clang_format.py --fix` formats everything; `--check` verifies. clang-format **18.1.8** comes from `uv.lock`, so local and CI formatting are identical.
- clang-tidy: `.\scripts\dev.ps1 tidy` (Windows), the `linux-tidy` preset, or clangd with `--clang-tidy` in your editor.
- `uv run python scripts/check_file_length.py` — code lines per file in `include/` and `src/`: warning > 150, error > 300.
- `uv run python scripts/check_layering.py` — modules may only include lower layers (see `docs/PLAN.md`).
- Optional: `uv run pre-commit install` runs the same scripts on every commit.

## Naming (enforced by clang-tidy)

Types `CamelCase`; functions/variables `snake_case`; private/protected members `snake_case_`; constants `kCamelCase`; namespaces `lower`; macros `FASTBUS_UPPER` (only for platform/compiler detection and build config).

## Commits and branches

- [Conventional Commits](https://www.conventionalcommits.org): `feat|fix|docs|test|refactor|perf|build|ci|chore(scope): summary`, scope = module (`core`, `platform`, `transport`, `api`, `idl`, `record`, `faults`, `ekf`, `tools`).
- Branches: `feat/m1-spsc-ring`, `fix/...`. One logical change per PR. Tests precede or accompany the feature they cover.
- No "wip" commits on `main`; no giant mixed commits.

## Adding a test

1. Put it in the right folder (`tests/unit`, `tests/integration`, `tests/stress`, ...).
2. Register it in that folder's `CMakeLists.txt`:
   ```cmake
   fastbus_add_test(fastbus_unit_my_feature
       SOURCES my_feature_test.cpp
       DEPS fastbus::core
       LABELS unit)
   ```
   `fastbus_add_test` applies the warning flags, links GoogleTest and registers each `TEST` with ctest.
3. Run a subset with `ctest --preset linux-gcc -L unit` or `-R MyFeature`.

## PR checklist

See [`.github/pull_request_template.md`](.github/pull_request_template.md) — includes the hot-path checklist (no allocation, exceptions, locks or syscalls on publish/loan/take/poll/dispatch).
