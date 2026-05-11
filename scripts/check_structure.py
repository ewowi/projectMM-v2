#!/usr/bin/env python3
"""Block tracked top-level paths not in the allowlist. New entries require an ADR."""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

ALLOWED = {
    ".github", ".githooks", "docs", "scripts", "src",
    ".gitignore", "CLAUDE.md", "LICENSE", "README.md",
    "mkdocs.yml", "platformio.ini", "pyproject.toml", "uv.lock",
}


def main(argv):
    out = subprocess.check_output(["git", "ls-files"], cwd=ROOT).decode()
    tops = {line.split("/", 1)[0] for line in out.splitlines() if line}
    failed = False
    for t in sorted(tops):
        if t not in ALLOWED:
            print(f"FAIL: top-level path not in allowlist: {t}")
            print(f"      add to scripts/check_structure.py with an ADR under docs/adr/")
            failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
