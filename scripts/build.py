#!/usr/bin/env python3
"""Build the PC environment via PlatformIO."""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def main(argv):
    cmd = ["pio", "run", "-e", "pc", *argv]
    return subprocess.call(cmd, cwd=ROOT)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
