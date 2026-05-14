#!/usr/bin/env python3
"""Reject platform guards outside src/pal/.

The v2 rule (see docs/architecture/system.md): `#ifdef ARDUINO`, `#include <Arduino.h>`,
ESP-IDF headers, and similar platform-identity gates appear ONLY in files under
src/pal/. Modules consume platform behaviour through the pal/ interface and contain
no platform conditionals themselves.

This rule preserves what v1 got right (one place to look for platform code) while
the per-pal-file LOC budgets in check_loc.py prevent v1's kitchen-sink Pal.h drift.

A new pal file is fine — it just needs a BUDGETS entry in check_loc.py.
A platform guard outside pal/ is a structural violation: either the guard should
move to a pal file, or the surrounding logic was always meant to be in pal/.
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Files under these paths are allowed to contain platform guards.
PAL_PREFIX = "src/pal/"

EXTS = {".h", ".hpp", ".cpp", ".cc"}

# Patterns that count as platform guards. Listed by name + regex.
PATTERNS = [
    ("#ifdef/#if defined ARDUINO",         re.compile(r"^\s*#\s*(ifdef|if\s+defined)\s*\(?\s*ARDUINO\b")),
    ("#ifdef/#if defined ESP_PLATFORM",    re.compile(r"^\s*#\s*(ifdef|if\s+defined)\s*\(?\s*ESP_PLATFORM\b")),
    ("#ifdef/#if defined ESP32",           re.compile(r"^\s*#\s*(ifdef|if\s+defined)\s*\(?\s*ESP32\b")),
    ("#ifdef/#if defined ARDUINO_ARCH_*",  re.compile(r"^\s*#\s*(ifdef|if\s+defined)\s*\(?\s*ARDUINO_ARCH_")),
    ("#include <Arduino.h>",               re.compile(r'^\s*#\s*include\s*[<"]Arduino\.h[>"]')),
    ("#include <esp_*.h> (ESP-IDF)",       re.compile(r'^\s*#\s*include\s*[<"]esp_[a-z_]+\.h[>"]')),
    ("#include <freertos/*.h>",            re.compile(r'^\s*#\s*include\s*[<"]freertos/')),
]


def main(argv):
    out = subprocess.check_output(["git", "ls-files", "src"], cwd=ROOT).decode()
    failed = False
    files_scanned = 0
    for rel in sorted(out.splitlines()):
        if not rel or rel.startswith(PAL_PREFIX):
            continue
        p = ROOT / rel
        if not p.is_file() or p.suffix not in EXTS:
            continue
        files_scanned += 1
        for lineno, line in enumerate(p.read_text().splitlines(), 1):
            for name, pat in PATTERNS:
                if pat.match(line):
                    print(f"FAIL: {rel}:{lineno}: platform guard outside src/pal/ — {name}")
                    print(f"      {line.rstrip()}")
                    failed = True
    if failed:
        print()
        print("Platform guards must live in src/pal/ files only. Move the conditional")
        print("logic into a pal file (e.g. src/pal/PalHttp.h) and call it via pal::* from")
        print("the module. See docs/architecture/system.md.")
    else:
        print(f"OK   {files_scanned} files scanned (src/, excluding src/pal/) — no platform guards")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
