#!/usr/bin/env python3
"""Forbid allocations and blocking calls inside any loop*() method body.

Detects: void [Class::]loop[20ms|1s|10s]() { ... } and scans the body for
banned patterns. Brace-balanced extraction; no false positives from strings
or comments are handled — keep loops free of those if it ever matters.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
EXTS = {".h", ".hpp", ".cpp", ".cc"}

LOOP_DECL = re.compile(
    r"\b(?:void|int|bool|auto)\s+(?:\w+::)?loop(?:20ms|1s|10s)?\s*\([^)]*\)\s*"
    r"(?:override\s*)?(?:final\s*)?\{"
)

BANNED = [
    (re.compile(r"\bnew\s+\w"),               "new"),
    (re.compile(r"\bmalloc\s*\("),            "malloc"),
    (re.compile(r"\bpsram_malloc\s*\("),      "psram_malloc"),
    (re.compile(r"\bJsonDocument\s*[<(]"),    "JsonDocument"),
    (re.compile(r"\bdelay\s*\("),             "delay"),
    (re.compile(r"\bvTaskDelay\s*\("),        "vTaskDelay"),
    (re.compile(r"\bsleep\s*\("),             "sleep"),
    (re.compile(r"\busleep\s*\("),            "usleep"),
    (re.compile(r"\brecv\s*\("),              "recv"),
]


def loop_bodies(text):
    for m in LOOP_DECL.finditer(text):
        open_brace = m.end() - 1
        depth = 0
        for i in range(open_brace, len(text)):
            if text[i] == "{":
                depth += 1
            elif text[i] == "}":
                depth -= 1
                if depth == 0:
                    yield m.start(), text[open_brace + 1:i]
                    break


def main(argv):
    failed = False
    loops_scanned = 0
    for f in (ROOT / "src").rglob("*"):
        if not f.is_file() or f.suffix not in EXTS:
            continue
        text = f.read_text()
        for decl_pos, body in loop_bodies(text):
            loops_scanned += 1
            line_no = text[:decl_pos].count("\n") + 1
            for pat, name in BANNED:
                if pat.search(body):
                    print(f"FAIL {f.relative_to(ROOT)}:{line_no}: `{name}` in loop*() body")
                    failed = True
    if not failed:
        print(f"OK   {loops_scanned} loop*() bodies scanned — no allocs or blocking calls")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
