#!/usr/bin/env python3
"""Stream serial output from a connected ESP32.

Usage:
    uv run scripts/monitor.py [env] [--port PORT]

Wraps `pio device monitor`, which reads monitor_speed and monitor_filters
from the env's [env:X] section in platformio.ini (so the
esp32_exception_decoder filter works for backtrace decoding). `env`
defaults to `esp32dev`. `--port` defaults to PlatformIO's autodetect; the
UI passes one explicitly via the global port selector.

Long-running: the UI sends SIGTERM via the process-group kill path when
the user clicks Stop.
"""
import argparse
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "build"))
from _pio import pio_bin  # noqa: E402  (shared helper lives in scripts/build/)

ROOT = Path(__file__).resolve().parent.parent.parent


def main(argv):
    p = argparse.ArgumentParser()
    p.add_argument("env", nargs="?", default="esp32dev")
    p.add_argument("--port", default=None)
    args = p.parse_args(argv)
    cmd = [pio_bin(), "device", "monitor", "-e", args.env]
    if args.port:
        cmd.extend(["--port", args.port])
    return subprocess.call(cmd, cwd=ROOT)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
