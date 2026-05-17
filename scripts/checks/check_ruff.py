#!/usr/bin/env python3
"""Run ruff over scripts/ and report findings. Ignores E402 and E70x (intentional style)."""
import subprocess
import sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent.parent
sys.exit(subprocess.call(
    ["uv", "run", "--with", "ruff", "ruff", "check", "scripts/",
     "--ignore", "E402,E701,E702"],
    cwd=ROOT,
))
