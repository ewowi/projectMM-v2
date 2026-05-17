#!/usr/bin/env python3
"""Run the projectMM binary (long-running; Ctrl-C or /stop to quit)."""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent


def main(argv):
    binary = ROOT / ".pio" / "build" / "pc" / "program"
    if not binary.exists():
        print("Binary not found; run Build first.")
        return 1
    return subprocess.call([str(binary), *argv])


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
