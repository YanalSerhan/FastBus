#!/usr/bin/env python3
"""Run clang-format over all first-party C++ sources.

Usage:
    python scripts/run_clang_format.py --check   # exit 1 if any file is mis-formatted
    python scripts/run_clang_format.py --fix     # rewrite files in place

Used by the CMake `format` / `format-check` targets, CI, pre-commit and scripts/dev.ps1, so
the file list lives in exactly one place.

clang-format lookup order: $CLANG_FORMAT, then PATH, then the usual Windows install locations
(LLVM installer, Visual Studio's "C++ Clang tools" component). CI uses clang-format 18; other
major versions can format slightly differently, so a version mismatch prints a warning.
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


def _windows_candidates() -> list[str]:
    patterns = [
        os.path.join(os.environ.get("ProgramFiles", r"C:\Program Files"), "LLVM", "bin",
                     "clang-format.exe"),
        r"C:\Program Files\Microsoft Visual Studio\2022\*\VC\Tools\Llvm\x64\bin\clang-format.exe",
        r"C:\Program Files (x86)\Microsoft Visual Studio\2022\*\VC\Tools\Llvm\x64\bin"
        r"\clang-format.exe",
    ]
    found: list[str] = []
    for pattern in patterns:
        found.extend(sorted(glob.glob(pattern)))
    return found


def find_clang_format() -> str:
    """Return a clang-format executable path."""
    names = [os.environ.get("CLANG_FORMAT"), "clang-format", f"clang-format-{REFERENCE_MAJOR}"]
    for name in names:
        if name:
            path = shutil.which(name)
            if path:
                return path
    if os.name == "nt":
        candidates = _windows_candidates()
        if candidates:
            return candidates[0]
    sys.exit(
        "error: clang-format not found.\n"
        "  Windows: `winget install LLVM.LLVM` or add the 'C++ Clang tools for Windows' "
        "component in the Visual Studio Installer.\n"
        "  Or set CLANG_FORMAT to the executable's full path."
    )


def warn_on_version_mismatch(clang_format: str) -> None:
    out = subprocess.run([clang_format, "--version"], capture_output=True, text=True,
                         check=False).stdout
    match = re.search(r"version (\d+)\.", out)
    if match and int(match.group(1)) != REFERENCE_MAJOR:
        print(f"warning: {clang_format} is version {match.group(1)}, CI uses "
              f"{REFERENCE_MAJOR}; results may differ slightly (docs/known-issues.md KI-002)",
              file=sys.stderr)


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
    warn_on_version_mismatch(clang_format)
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
              "`python scripts/run_clang_format.py --fix`", file=sys.stderr)
    elif status == 0:
        print(f"{'checked' if args.check else 'formatted'} {len(files)} files")
    return 1 if status else 0


if __name__ == "__main__":
    sys.exit(main())
