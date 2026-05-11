#!/usr/bin/env python3
"""scripts/mkdocs_serve.py — run mkdocs serve on 127.0.0.1:8000. Long-running.

`os.execvp` replaces this Python process with `uv` so the process chain stays
short (one uv → mkdocs), making clean shutdown via process-group signal reliable.
"""
import os
import sys

if __name__ == "__main__":
    os.execvp("uv", ["uv", "run", "--extra", "docs", "mkdocs", "serve",
                     "--dev-addr", "127.0.0.1:8000"])
    sys.exit(1)  # unreachable, but tells readers what failure looks like
