#!/usr/bin/env python3
"""Smoke test: build the PC env and run the resulting binary."""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def main(argv):
    rc = subprocess.call(["pio", "run", "-e", "pc"], cwd=ROOT)
    if rc != 0:
        return rc
    binary = ROOT / ".pio" / "build" / "pc" / "program"
    return subprocess.call([str(binary), *argv])


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
