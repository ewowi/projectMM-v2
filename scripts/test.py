#!/usr/bin/env python3
"""Run host unit + integration tests via doctest (env:pc-test)."""
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _pio import pio_bin

ROOT = Path(__file__).resolve().parent.parent

_TAGS = ("[PASSED]", "[FAILED]", "[SKIPPED]")

_TYPE = {
    "test_module": "unit",
    "test_hello":  "unit",
    "test_http":   "integration",
}


def _parse(line):
    """Return (file, description, type, status) for a result line, else None."""
    if "\t" not in line or not any(t in line for t in _TAGS):
        return None
    left, status = line.split("\t", 1)
    m = re.match(r"^(.+:\d+): (.+)$", left)
    if not m:
        return None
    filepath = m.group(1)
    stem = Path(filepath.split(":")[0]).stem
    return filepath, m.group(2), _TYPE.get(stem, "?"), status


def _align(lines):
    rows = [_parse(l) for l in lines]
    result_rows = [r for r in rows if r is not None]
    if not result_rows:
        return lines
    wf = max(len(r[0]) for r in result_rows)
    wd = max(len(r[1]) for r in result_rows)
    wt = max(len(r[2]) for r in result_rows)
    out = []
    for line, row in zip(lines, rows):
        if row is None:
            out.append(line)
        else:
            f, d, t, s = row
            out.append(f.ljust(wf + 2) + d.ljust(wd + 2) + t.ljust(wt + 2) + s)
    return out


def main(argv):
    proc = subprocess.Popen(
        [pio_bin(), "test", "-e", "pc-test", *argv],
        cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, bufsize=1,
    )
    lines = [l.rstrip("\n") for l in proc.stdout]
    rc = proc.wait()
    for line in _align(lines):
        print(line, flush=True)
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
