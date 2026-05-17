#!/usr/bin/env python3
"""Forbid raw integer pin literals in GPIO calls. Pins must come from typed config."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
EXTS = {".h", ".hpp", ".cpp", ".cc"}

PATTERNS = [
    re.compile(r"\bpinMode\s*\(\s*\d"),
    re.compile(r"\bdigitalWrite\s*\(\s*\d"),
    re.compile(r"\bdigitalRead\s*\(\s*\d"),
    re.compile(r"\banalogRead\s*\(\s*\d"),
    re.compile(r"\banalogWrite\s*\(\s*\d"),
    re.compile(r"\bgpio_set_level\s*\(\s*\d"),
    re.compile(r"\bgpio_set_direction\s*\(\s*\d"),
    re.compile(r"\bgpio_get_level\s*\(\s*\d"),
]


def main(argv):
    failed = False
    files_scanned = 0
    for f in (ROOT / "src").rglob("*"):
        if not f.is_file() or f.suffix not in EXTS:
            continue
        files_scanned += 1
        for i, line in enumerate(f.read_text().splitlines(), 1):
            for pat in PATTERNS:
                if pat.search(line):
                    print(f"FAIL {f.relative_to(ROOT)}:{i}: raw GPIO pin literal: {line.strip()}")
                    failed = True
    if not failed:
        print(f"OK   {files_scanned} files scanned — no raw GPIO pin literals")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
