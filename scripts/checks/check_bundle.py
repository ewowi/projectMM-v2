#!/usr/bin/env python3
"""Fail if src/frontend/frontend_bundle.h is out of sync with its sources.

Compares the deterministic `// RawSHA256:` line in the committed header
against a fresh SHA-256 of the inlined sources. Drift means a frontend
source was edited without running gen_frontend_bundle.py. Run by pre-commit
and CI; fix by:

    uv run scripts/gen_frontend_bundle.py

Why the raw SHA and not a byte-for-byte header compare: the header also
contains the gzip-compressed C array, and zlib's output is NOT byte-stable
across zlib versions (e.g. macOS zlib vs the CI Ubuntu zlib differ for
identical input). Comparing that array produced false CI failures on
unchanged sources. The raw inlined bytes ARE deterministic everywhere, so
their SHA is the drift contract; the gzip array is a build artifact.
"""
import re
import sys
from pathlib import Path

# gen_frontend_bundle lives in scripts/build/ (this check is in scripts/checks/).
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "build"))
import gen_frontend_bundle as gen  # noqa: E402


def main() -> int:
    raw, _ = gen.build_bundle()
    expected = gen.raw_sha(raw)
    text = gen.OUT_HEADER.read_text(encoding="utf-8") if gen.OUT_HEADER.exists() else ""
    m = re.search(r"//\s*RawSHA256:\s*([0-9a-f]{64})", text)
    committed = m.group(1) if m else None
    if committed == expected:
        print("[check_bundle] frontend_bundle.h matches sources ✓")
        return 0
    if committed is None:
        print("FAIL: src/frontend/frontend_bundle.h missing or has no RawSHA256 line.")
    else:
        print("FAIL: src/frontend/frontend_bundle.h is out of sync with its sources.")
    print("      run: uv run scripts/gen_frontend_bundle.py")
    return 1


if __name__ == "__main__":
    sys.exit(main())
