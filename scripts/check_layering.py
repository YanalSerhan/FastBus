#!/usr/bin/env python3
"""Check that #include "fastbus/<module>/..." edges respect the layering rules (G0.2).

Rules (docs/PLAN.md):
  * a library module may include only itself and the modules listed in ALLOWED[module];
  * tools/ may use api + record (+ idl for generated messages);
  * examples/ and modules/ekf may use only the public API (api + idl).
`fastbus/version.hpp` (generated) is allowed everywhere.

ADR-002 exception: platform may include the dependency-free vocabulary headers
`fastbus/core/{result,error,assert}.hpp` (library `fastbus::vocabulary`) and nothing else from core.

Also enforces ADR-007: OS/std clocks (`steady_clock`, `system_clock`, `high_resolution_clock`)
may only be used in src/platform/ and benchmarks/; everything else takes a `core::Clock&`.
Exit code 1 on any violation.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*"fastbus/([a-z_]+)/([A-Za-z0-9_./]+)"', re.MULTILINE)
VOCABULARY = {"result.hpp", "error.hpp", "assert.hpp"}  # fastbus/core/<these>, ADR-002
EXTENSIONS = {".hpp", ".cpp", ".h", ".ipp"}
BANNED_CLOCK_RE = re.compile(r"\b(steady_clock|system_clock|high_resolution_clock)\b")
CLOCK_ALLOWED_PREFIXES = ("src/platform/", "benchmarks/")
CLOCK_CHECKED_TOPS = ("include", "src", "tools", "examples", "modules", "tests")

# Direct + transitive dependencies per library module (lower layers only).
ALLOWED: dict[str, set[str]] = {
    "platform": set(),
    "core": {"platform"},
    "transport": {"core", "platform"},
    "faults": {"transport", "core", "platform"},
    "record": {"transport", "core", "platform"},
    "idl": {"core", "platform"},
    "api": {"transport", "faults", "idl", "core", "platform"},
}
CONSUMERS: dict[str, set[str]] = {
    "tools": {"api", "record", "idl"},
    "examples": {"api", "idl"},
    "modules/ekf": {"api", "idl"},
}


def owner_of(rel: Path) -> tuple[str, set[str]] | None:
    """Return (owner name, allowed modules) for a source file, or None if unchecked."""
    parts = rel.parts
    if len(parts) >= 3 and parts[0] == "include" and parts[1] == "fastbus" and parts[2] in ALLOWED:
        return parts[2], ALLOWED[parts[2]] | {parts[2]}
    if len(parts) >= 2 and parts[0] == "src" and parts[1] in ALLOWED:
        return parts[1], ALLOWED[parts[1]] | {parts[1]}
    posix = rel.as_posix()
    for prefix, allowed in CONSUMERS.items():
        if posix.startswith(prefix + "/"):
            return prefix, allowed
    return None


def clock_violations() -> int:
    """ADR-007: count (and print) direct uses of std clocks outside the allowed places."""
    found = 0
    for top in CLOCK_CHECKED_TOPS:
        for path in sorted((ROOT / top).rglob("*")):
            if path.suffix not in EXTENSIONS or not path.is_file():
                continue
            rel = path.relative_to(ROOT).as_posix()
            if rel.startswith(CLOCK_ALLOWED_PREFIXES):
                continue
            for lineno, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
                if BANNED_CLOCK_RE.search(line):
                    found += 1
                    print(f"error: {rel}:{lineno}: use fastbus::core::Clock instead of std clocks "
                          "(ADR-007)")
    return found


def main() -> int:
    violations = clock_violations()
    checked = 0
    for top in ("include", "src", "tools", "examples", "modules"):
        for path in sorted((ROOT / top).rglob("*")):
            if path.suffix not in EXTENSIONS or not path.is_file():
                continue
            rel = path.relative_to(ROOT)
            owner = owner_of(rel)
            if owner is None:
                continue
            name, allowed = owner
            checked += 1
            for module, header in INCLUDE_RE.findall(path.read_text(encoding="utf-8")):
                if name == "platform" and module == "core" and header in VOCABULARY:
                    continue
                if module not in allowed:
                    violations += 1
                    print(f"error: {rel.as_posix()}: '{name}' must not include fastbus/{module}/")
    print(f"layering: checked {checked} files, {violations} violations")
    return 1 if violations else 0


if __name__ == "__main__":
    sys.exit(main())
