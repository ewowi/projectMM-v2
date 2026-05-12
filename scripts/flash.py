#!/usr/bin/env python3
"""Flash a PlatformIO ESP32 environment over USB.

Usage:
    uv run scripts/flash.py [env] [--port PORT]

`env` defaults to `esp32dev`. `--port` defaults to PlatformIO's autodetect.
The UI passes a port explicitly via the global port selector.
"""
import argparse
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _pio import pio_bin

ROOT = Path(__file__).resolve().parent.parent


def main(argv):
    p = argparse.ArgumentParser()
    p.add_argument("env", nargs="?", default="esp32dev")
    p.add_argument("--port", default=None)
    args = p.parse_args(argv)
    cmd = [pio_bin(), "run", "-e", args.env, "--target", "upload"]
    if args.port:
        cmd.extend(["--upload-port", args.port])
    return subprocess.call(cmd, cwd=ROOT)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
