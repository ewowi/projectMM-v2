#!/usr/bin/env python3
"""Fail if src/frontend/frontend_bundle.h is out of sync with its sources.

Re-generates the bundle in-memory from src/frontend/{index.html,style.css,
app.js} and compares against the committed bundle byte-for-byte. Drift
means a frontend source was edited without running gen_frontend_bundle.py.
Run by pre-commit and CI; fix by:

    uv run scripts/gen_frontend_bundle.py

The generator is deterministic (gzip mtime=0), so identical sources always
produce identical bundles.
"""
import sys

import gen_frontend_bundle as gen


def main() -> int:
    raw, gz = gen.build_bundle()
    expected = gen.render_header(len(raw), gz)
    actual = gen.OUT_HEADER.read_text(encoding="utf-8") if gen.OUT_HEADER.exists() else ""
    if expected == actual:
        print("[check_bundle] frontend_bundle.h matches sources ✓")
        return 0
    print("FAIL: src/frontend/frontend_bundle.h is out of sync with its sources.")
    print("      run: uv run scripts/gen_frontend_bundle.py")
    return 1


if __name__ == "__main__":
    sys.exit(main())
