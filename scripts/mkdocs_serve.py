#!/usr/bin/env python3
"""scripts/mkdocs_serve.py — run mkdocs serve on 127.0.0.1:8000. Long-running.

Kills any process already bound to :8000 before starting, so pressing Start
twice (or restarting ui.py with a stale mkdocs) never fails with
"Address already in use".

`os.execvp` then replaces this process with `uv run mkdocs serve`, keeping the
chain short (one uv → mkdocs) so process-group shutdown from ui.py is clean.
"""
import os
import signal
import subprocess
import sys
import time


def _free_port(port: int) -> None:
    try:
        result = subprocess.run(
            ["lsof", "-ti", f"tcp:{port}"],
            capture_output=True, text=True,
        )
        pids = [int(p) for p in result.stdout.split() if p.strip().isdigit()]
        for pid in pids:
            try:
                os.kill(pid, signal.SIGTERM)
                print(f"[mkdocs] killed stale process {pid} on :{port}")
            except ProcessLookupError:
                pass
        if pids:
            time.sleep(0.5)
    except FileNotFoundError:
        pass  # lsof not available


if __name__ == "__main__":
    _free_port(8000)
    os.execvp("uv", ["uv", "run", "--extra", "docs", "mkdocs", "serve",
                     "--dev-addr", "127.0.0.1:8000"])
    sys.exit(1)  # unreachable
