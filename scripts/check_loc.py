#!/usr/bin/env python3
"""Check LOC budgets per surface. Non-blank lines per directory, .h/.hpp/.cpp/.cc only."""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

BUDGETS = {
    "src/core": 300,
    "src/pal": 200,
}

EXTS = {".h", ".hpp", ".cpp", ".cc"}


def count_loc(path: Path) -> int:
    total = 0
    for f in path.rglob("*"):
        if not f.is_file() or f.suffix not in EXTS:
            continue
        for line in f.read_text().splitlines():
            s = line.strip()
            if not s or s.startswith("//"):
                continue
            total += 1
    return total


def main(argv):
    failed = False
    for path_str, limit in BUDGETS.items():
        path = ROOT / path_str
        if not path.exists():
            print(f"SKIP: {path_str} (does not exist)")
            continue
        loc = count_loc(path)
        status = "OK  " if loc <= limit else "OVER"
        print(f"{status} {path_str}: {loc} / {limit}")
        if loc > limit:
            failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
