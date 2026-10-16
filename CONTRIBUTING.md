# Contributing to fastbus

## Build

All builds go through CMake presets; each preset builds into `build/<preset>/`. In-source builds are refused.

| OS | Preset | Notes |
|---|---|---|
| Windows | `windows-msvc` | Visual Studio 2022 generator (primary). Build presets `windows-msvc` (Debug) and `windows-msvc-release`. |
| Windows | `windows-clangcl` | clang-cl + Ninja; run from *Developer PowerShell for VS 2022*. Emits `compile_commands.json` for clangd / clang-tidy. |
| Windows | `windows-asan` | MSVC AddressSanitizer (optional). |
| Linux | `linux-gcc`, `linux-clang` | Ninja, Debug. |
| Linux | `linux-asan`, `linux-ubsan`, `linux-tsan` | Clang + sanitizer; includes the canary for that sanitizer. |
| Linux | `linux-coverage` | GCC + `--coverage`; `cmake --build --preset linux-coverage --target coverage` (needs `gcovr`). |
| Linux | `linux-tidy` | Clang with clang-tidy run on every TU during the build. |
| Linux | `linux-bench` | GCC Release with frame pointers, for benchmarks/profiling. |

```bash
cmake --preset <preset>
cmake --build --preset <preset>
ctest --preset <preset>
```

Local overrides: copy `CMakeUserPresets.json.example` to `CMakeUserPresets.json` (gitignored).

### Linux presets from Windows (WSL2)

```powershell
wsl --install -d Ubuntu-24.04          # once
```
```bash
# inside WSL — clone into the Linux filesystem (~/src), not /mnt/c, for speed and correct permissions
sudo apt update && sudo apt install -y build-essential clang clang-tidy-18 clang-format-18 \
     libclang-rt-18-dev cmake ninja-build python3 python3-pip git
git clone https://github.com/YanalSerhan/FastBus.git ~/src/FastBus && cd ~/src/FastBus
cmake --preset linux-clang && cmake --build --preset linux-clang && ctest --preset linux-clang
```

**TSan on WSL2 / recent kernels:** if TSan aborts with `unexpected memory mapping`, run `sudo sysctl vm.mmap_rnd_bits=28` (resets on reboot).

## Formatting and lint

- `cmake --build --preset <p> --target format` (or `python scripts/run_clang_format.py --fix`) formats everything; `--target format-check` / `--check` verifies. clang-format **18** is the reference version.
- clang-tidy: use the `linux-tidy` preset, or clangd with `--clang-tidy` in your editor.
- `python scripts/check_file_length.py` — code lines per file in `include/` and `src/`: warning > 150, error > 300.
- `python scripts/check_layering.py` — modules may only include lower layers (see `docs/PLAN.md`).
- Optional: `pre-commit install` runs the same scripts on commit.

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
