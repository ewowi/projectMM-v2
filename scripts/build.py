#!/usr/bin/env python3
"""Build a PlatformIO environment.

Default env is `pc`; pass an env name as the first arg to build another
(`esp32dev`, `esp32s3_n16r8`). CI uses this to build all three envs.
"""
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _pio import pio_bin

ROOT = Path(__file__).resolve().parent.parent


def main(argv):
    env = argv[0] if argv else "pc"
    rest = argv[1:] if argv else []
    cmd = [pio_bin(), "run", "-e", env, *rest]
    return subprocess.call(cmd, cwd=ROOT)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
