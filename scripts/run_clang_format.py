#!/usr/bin/env python3
"""Run clang-format over all first-party C++ sources.

Usage:
    python scripts/run_clang_format.py --check   # exit 1 if any file is mis-formatted
    python scripts/run_clang_format.py --fix     # rewrite files in place

Used by the CMake `format` / `format-check` targets, CI, pre-commit and scripts/dev.ps1, so
the file list lives in exactly one place.

Run it through uv so the pinned clang-format 18 from uv.lock is used:
    uv run python scripts/run_clang_format.py --check

clang-format lookup: $CLANG_FORMAT, PATH (which `uv run` puts .venv first on), the environment's
own Scripts/bin folder, then on Windows the LLVM installer and Visual Studio's "C++ Clang
tools". Version 18 (what CI uses) is preferred; any other major version formats slightly
differently, so using one prints a warning.
"""

from __future__ import annotations

import argparse
import glob
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE_DIRS = ("include", "src", "tests", "benchmarks", "examples", "tools", "modules")
EXTENSIONS = {".hpp", ".cpp", ".h", ".cc", ".ipp"}
EXCLUDED_PARTS = {"build", "_deps", "golden"}  # golden files are byte-exact fixtures
REFERENCE_MAJOR = 18
BATCH_SIZE = 50  # keeps the command line far below Windows' 32 767-character limit


def _major_version(exe: str) -> int | None:
    try:
        out = subprocess.run([exe, "--version"], capture_output=True, text=True,
                             check=False).stdout
    except OSError:
        return None
    match = re.search(r"version (\d+)\.", out)
    return int(match.group(1)) if match else None


def _candidates() -> list[str]:
    """Every clang-format we can find, in priority order, without duplicates."""
    found: list[str] = []
    env = os.environ.get("CLANG_FORMAT")
    if env:
        found.append(shutil.which(env) or env)
    for name in (f"clang-format-{REFERENCE_MAJOR}", "clang-format"):
        path = shutil.which(name)
        if path:
            found.append(path)
    # The interpreter's own folder: .venv/Scripts (Windows) or .venv/bin when run via uv.
    py_dir = Path(sys.executable).parent
    found += [str(py_dir / "clang-format.exe"), str(py_dir / "clang-format")]
    if os.name == "nt":
        found.append(os.path.join(os.environ.get("ProgramFiles", r"C:\Program Files"), "LLVM",
                                  "bin", "clang-format.exe"))
        for pattern in (r"C:\Program Files\Microsoft Visual Studio\2022\*\VC\Tools\Llvm\x64"
                        r"\bin\clang-format.exe",
                        r"C:\Program Files (x86)\Microsoft Visual Studio\2022\*\VC\Tools\Llvm"
                        r"\x64\bin\clang-format.exe"):
            found += sorted(glob.glob(pattern))
    unique: list[str] = []
    for path in found:
        if os.path.isfile(path) and path not in unique:
            unique.append(path)
    return unique


def find_clang_format() -> str:
    """Prefer clang-format REFERENCE_MAJOR (what CI uses); fall back to any other version."""
    candidates = _candidates()
    if not candidates:
        sys.exit(
            "error: clang-format not found.\n"
            "  Run the script through uv, which provides the pinned version:\n"
            "    uv run python scripts/run_clang_format.py --check\n"
            "  Or set CLANG_FORMAT to the executable's full path."
        )
    for path in candidates:
        if _major_version(path) == REFERENCE_MAJOR:
            return path
    chosen = candidates[0]
    print(f"warning: no clang-format {REFERENCE_MAJOR} found, using {chosen} (version "
          f"{_major_version(chosen)}); results may differ from CI (docs/known-issues.md KI-002).\n"
          "         Fix: run through uv: uv run python scripts/run_clang_format.py", file=sys.stderr)
    return chosen


def collect_sources() -> list[Path]:
    """All first-party C++ files, sorted for deterministic output."""
    files: list[Path] = []
    for directory in SOURCE_DIRS:
        base = ROOT / directory
        if not base.is_dir():
            continue
        for path in base.rglob("*"):
            rel_parts = set(path.relative_to(ROOT).parts)
            if path.suffix in EXTENSIONS and not EXCLUDED_PARTS & rel_parts:
                files.append(path)
    return sorted(files)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true", help="report mis-formatted files")
    mode.add_argument("--fix", action="store_true", help="format files in place")
    args = parser.parse_args()

    clang_format = find_clang_format()
    files = collect_sources()
    if not files:
        print("no sources found")
        return 0

    flags = ["--dry-run", "--Werror"] if args.check else ["-i"]
    status = 0
    for start in range(0, len(files), BATCH_SIZE):
        batch = [str(f) for f in files[start:start + BATCH_SIZE]]
        status |= subprocess.run([clang_format, *flags, *batch], check=False).returncode
    if args.check and status != 0:
        print("\nformat-check failed: run `.\\scripts\\dev.ps1 format` (Windows) or "
              "`uv run python scripts/run_clang_format.py --fix`", file=sys.stderr)
    elif status == 0:
        print(f"{'checked' if args.check else 'formatted'} {len(files)} files")
    return 1 if status else 0


if __name__ == "__main__":
    sys.exit(main())
