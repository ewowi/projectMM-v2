"""Return the PlatformIO CLI binary that uses PlatformIO's own bundled Python.

On macOS with Homebrew, `which pio` typically resolves to a launcher with a
`#!/opt/homebrew/opt/python@3.12/...` shebang. That Python's site-packages
can contain a global `fatfs` module that does not match the version the
espressif32 platform was packaged for (PlatformIO's own Python 3.11 in
~/.platformio/penv), and the build fails with:

    ImportError: cannot import name 'create_extended_partition' from 'fatfs'

Invoking pio via the penv directly side-steps the system Python entirely.
Falls back to PATH lookup when the penv binary isn't present (CI containers
that install PlatformIO via pip don't have one).
"""
from pathlib import Path


def pio_bin() -> str:
    penv = Path.home() / ".platformio" / "penv" / "bin" / "pio"
    return str(penv) if penv.exists() else "pio"
