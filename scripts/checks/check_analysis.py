#!/usr/bin/env python3
"""Run lizard cyclomatic-complexity analysis over src/ and print a per-file summary."""
import subprocess
import sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent.parent
sys.exit(subprocess.call(
    ["uv", "run", "--with", "lizard", "python3", "-m", "lizard", "src/", "-l", "cpp",
     "--ignore_warnings", "1", "-x", "src/frontend/*"],
    cwd=ROOT,
))
