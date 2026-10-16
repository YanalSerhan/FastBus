#!/usr/bin/env python3
"""Enforce the file-length policy (TODO.md Q3) on hand-written library code.

Counts *code* lines (excluding blank lines and comment-only lines) in include/ and src/.
  > SOFT_LIMIT  -> warning (GitHub annotation in CI)
  > HARD_LIMIT  -> error, exit code 1
Tests, benchmarks, generated code and platform backends are exempt (documented in
docs/known-issues.md). Individual files can be exempted via EXEMPT_FILES with a reason.
"""

from __future__ import annotations

import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CHECKED_DIRS = ("include", "src")
EXTENSIONS = {".hpp", ".cpp", ".h", ".ipp", ".in"}
SOFT_LIMIT = 150
HARD_LIMIT = 300
EXEMPT_DIRS = ("src/platform/windows", "src/platform/posix")
EXEMPT_FILES: dict[str, str] = {
    # "src/core/example.cpp": "reason",
}

_BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.DOTALL)


def count_code_lines(text: str) -> int:
    """Number of lines that contain something other than whitespace or comments."""
    text = _BLOCK_COMMENT.sub(lambda m: "\n" * m.group(0).count("\n"), text)
    count = 0
    for line in text.splitlines():
        stripped = line.strip()
        if stripped and not stripped.startswith("//"):
            count += 1
    return count


def is_exempt(rel: str) -> bool:
    return rel in EXEMPT_FILES or any(rel.startswith(d + "/") for d in EXEMPT_DIRS)


def main() -> int:
    in_ci = os.environ.get("GITHUB_ACTIONS") == "true"
    errors = 0
    checked = 0
    for directory in CHECKED_DIRS:
        for path in sorted((ROOT / directory).rglob("*")):
            if path.suffix not in EXTENSIONS or not path.is_file():
                continue
            rel = path.relative_to(ROOT).as_posix()
            if is_exempt(rel):
                continue
            checked += 1
            lines = count_code_lines(path.read_text(encoding="utf-8"))
            if lines > HARD_LIMIT:
                errors += 1
                msg = f"{lines} code lines > hard limit {HARD_LIMIT}"
                print(f"::error file={rel}::{msg}" if in_ci else f"error: {rel}: {msg}")
            elif lines > SOFT_LIMIT:
                msg = f"{lines} code lines > soft limit {SOFT_LIMIT}"
                print(f"::warning file={rel}::{msg}" if in_ci else f"warning: {rel}: {msg}")
    print(f"file-length: checked {checked} files, {errors} over the hard limit")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
