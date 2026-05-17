#!/usr/bin/env python3
"""Run host unit + integration tests via doctest (env:pc-test).

Streams `pio test` output line-by-line as the runner produces it — earlier
versions collected every line first to compute aligned columns, which made
the moondeck.py log window appear blank until the whole 5 s run finished. The
classification badge that the previous alignment carried (unit / integration)
moved to `scripts/classify_tests.py` in Sprint 8 Rail 1; pipe through it
when you want the badge:

    uv run scripts/test.py | uv run scripts/classify_tests.py
"""
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _pio import pio_bin

ROOT = Path(__file__).resolve().parent.parent.parent


def main(argv):
    proc = subprocess.Popen(
        [pio_bin(), "test", "-e", "pc-test", *argv],
        cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, bufsize=1,
    )
    for line in proc.stdout:
        print(line.rstrip("\n"), flush=True)
    return proc.wait()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
