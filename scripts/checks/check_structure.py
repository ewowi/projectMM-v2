#!/usr/bin/env python3
"""Block tracked top-level paths not in the allowlist. New entries require an ADR."""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent

ALLOWED = {
    ".github", ".githooks", "data", "docs", "lib", "partitions", "scripts", "src", "test",
    ".gitignore", "CLAUDE.md", "LICENSE", "README.md",
    "mkdocs.yml", "platformio.ini", "pyproject.toml", "uv.lock",
}
# Each entry is justified by either being a v1 carry-over or by a paired ADR.
# "lib"        → docs/develop/adr/0001-vendor-cpp-httplib.md (vendored third-party only).
# "partitions" → ESP32 board flash layouts (CSV); pioarduino requires a file
#                path, no .ini equivalent. Carry-over from v1 (Sprint 4).
# "data"       → source dir for the LittleFS image (`pio run -t uploadfs`).
#                Only wifi.json.example is committed; wifi.json is gitignored.
#                Sprint 5.


def main(argv):
    out = subprocess.check_output(["git", "ls-files"], cwd=ROOT).decode()
    tops = {line.split("/", 1)[0] for line in out.splitlines() if line}
    failed = False
    for t in sorted(tops):
        if t not in ALLOWED:
            print(f"FAIL: top-level path not in allowlist: {t}")
            print("      add to scripts/check_structure.py with an ADR under docs/develop/adr/")
            failed = True
    if not failed:
        print(f"OK   {len(tops)} top-level paths — all in allowlist ({', '.join(sorted(tops))})")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
