#!/usr/bin/env python3
"""Classify doctest TEST_CASE names by complexity and print one badge per case.

Reads `pio test -e pc-test` (or any doctest binary) output on stdin, recognises
result lines of the shape

    test/test_pc/<file>.cpp:<line>: <case-name>\t[PASSED|FAILED]

and re-emits each one with a leading badge:

    [smoke]      — does-not-crash, lifecycle
    [format]     — schema/string shape, no behavior
    [behavioral] — actual output values, state transitions  (default)
    [integration]— multi-module pipeline, cross-module wiring

Keys ported from v1's deploy/unittest.py. Output goes to stdout; nothing is
written to disk. Read live in moondeck.py's log window — that's the consumption
point. Run with: `pio test -e pc-test 2>&1 | scripts/classify_tests.py`.
"""
import re
import sys

_INTEGRATION_KEYS = ("pipeline", "loopback", "blend", "phase ", "integration",
                     "concurrent", "e2e", "two producers", "single producer",
                     "consumer sees", "checksum match", "both effects",
                     "blending", "preview pipeline", "cross-device", "cross-core",
                     "round-trip", "round trip", "scenario", "spsc")
_SMOKE_KEYS       = ("no crash", "should not crash", "does not crash",
                     "zero residue", "lifecycle", "does not write", "not write")
_FORMAT_KEYS      = ("format", "schema", "category is", "startswith",
                     "contains expected", "healthreport", "getschema",
                     "has no heap", "classsize", "wire format")

# PIO's test runner wraps doctest output as `<file>:<line>: <name>\t[STATUS]`.
_RESULT = re.compile(r'^(test/test_pc/[^:]+\.cpp):(\d+):\s+(.+?)\s+\[(PASSED|FAILED)\]\s*$')


def classify(name: str) -> str:
    n = name.lower()
    if any(k in n for k in _INTEGRATION_KEYS):
        return "integration"
    if any(k in n for k in _SMOKE_KEYS):
        return "smoke"
    if any(k in n for k in _FORMAT_KEYS):
        return "format"
    return "behavioral"


def main() -> int:
    for line in sys.stdin:
        m = _RESULT.match(line)
        if m:
            file_, _line, name, status = m.group(1), m.group(2), m.group(3), m.group(4)
            print(f"[{classify(name):<11}] [{status}] {name}  ({file_})")
        else:
            sys.stdout.write(line)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
