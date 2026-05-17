#!/usr/bin/env python3
"""List all PATCH: markers in src/ and src/frontend/.

Convention: any intentional workaround that has a stated unlock condition
should be tagged with a comment of the form (C++ or Python):
    [//|#] PATCH: <name> — <reason>. Remove when <condition>.

This script greps for those markers and prints a table. It is informational
only (exit 0 always) — patches are tracked, not failed, because they are
documented workarounds, not regressions. The companion backlog entries in
docs/development/backlog.md carry the unlock conditions.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent

SEARCH_DIRS = [
    ROOT / "src",
    ROOT / "scripts",
]
EXTS = {".h", ".hpp", ".cpp", ".cc", ".js", ".py"}

# Matches lines where PATCH: appears inside a comment (// or #), not in strings or docs.
PATCH_RE = re.compile(r"(?://|#)\s*PATCH:\s*(.+)")


def scan():
    hits = []
    for d in SEARCH_DIRS:
        for path in sorted(d.rglob("*")):
            if path.suffix not in EXTS:
                continue
            try:
                lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
            except OSError:
                continue
            for lineno, line in enumerate(lines, 1):
                m = PATCH_RE.search(line)
                if m:
                    rel = path.relative_to(ROOT)
                    hits.append((str(rel), lineno, m.group(1).strip()))
    return hits


def main():
    hits = scan()
    if not hits:
        print("No PATCH: markers found.")
        return 0

    print(f"Found {len(hits)} known patch(es):\n")
    for rel, lineno, desc in hits:
        print(f"  {rel}:{lineno}")
        print(f"    {desc}")
        print()
    print("Each patch has an unlock condition in docs/development/backlog.md")
    print("under 'Known patches — tracked for removal'.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
