#!/usr/bin/env python3
"""Fail when a source folder holds more than 12 files or a source file runs past 1000 lines."""
import sys
from pathlib import Path

MAX_FILES, MAX_LINES = 12, 1000
ROOTS = ("src", "tests", "tools")
root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent

problems = []
for name in ROOTS:
    for folder in [root / name, *(p for p in (root / name).rglob("*") if p.is_dir())]:
        files = [p for p in folder.iterdir() if p.is_file()]
        if len(files) > MAX_FILES:
            problems.append(f"{folder.relative_to(root)}: {len(files)} files")
        for f in files:
            lines = f.read_bytes().count(b"\n")
            if lines > MAX_LINES:
                problems.append(f"{f.relative_to(root)}: {lines} lines")

print("\n".join(problems) or "layout ok")
sys.exit(1 if problems else 0)
