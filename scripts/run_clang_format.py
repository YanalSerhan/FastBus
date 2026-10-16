#!/usr/bin/env python3
"""Run clang-format over all first-party C++ sources.

Usage:
    python scripts/run_clang_format.py --check   # exit 1 if any file is mis-formatted
    python scripts/run_clang_format.py --fix     # rewrite files in place

Used by the CMake `format` / `format-check` targets, CI and pre-commit, so the file list
lives in exactly one place. Set CLANG_FORMAT to pick a specific binary (e.g. clang-format-18).
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE_DIRS = ("include", "src", "tests", "benchmarks", "examples", "tools", "modules")
EXTENSIONS = {".hpp", ".cpp", ".h", ".cc", ".ipp"}
EXCLUDED_PARTS = {"build", "_deps", "golden"}  # golden files are byte-exact fixtures


def find_clang_format() -> str:
    """Return the clang-format executable, honouring $CLANG_FORMAT."""
    candidates = [os.environ.get("CLANG_FORMAT"), "clang-format", "clang-format-18"]
    for candidate in candidates:
        if candidate and shutil.which(candidate):
            return candidate
    sys.exit("error: clang-format not found (set CLANG_FORMAT or install clang-format 18)")


def collect_sources() -> list[Path]:
    """All first-party C++ files, sorted for deterministic output."""
    files: list[Path] = []
    for directory in SOURCE_DIRS:
        base = ROOT / directory
        if not base.is_dir():
            continue
        for path in base.rglob("*"):
            if path.suffix in EXTENSIONS and not EXCLUDED_PARTS & set(path.parts):
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
    result = subprocess.run([clang_format, *flags, *map(str, files)], check=False)
    if args.check and result.returncode != 0:
        print("\nformat-check failed: run `cmake --build <dir> --target format` "
              "or `python scripts/run_clang_format.py --fix`", file=sys.stderr)
    elif result.returncode == 0:
        print(f"{'checked' if args.check else 'formatted'} {len(files)} files")
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
