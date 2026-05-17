#!/usr/bin/env python3
"""Generate docs/developer-guide/tests.md — the per-module/core list of what
the unit tests verify, so users can see the tested surface.

Static: parses TEST_CASE("...") / SUBCASE("...") strings out of
test/test_pc/*.cpp. No build, no run, no JsonReporter, no log file — unlike
v1's unittest.py this does NOT execute anything. Re-run after adding or
renaming a test; the page is committed.

Usage:
    uv run scripts/build/gen_test_list.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
TEST_DIR = ROOT / "test" / "test_pc"
OUT = ROOT / "docs" / "developer-guide" / "tests.md"

# test file -> the module / core surface it covers (the heading users read).
# A file with no entry falls back to its filename so a new test still shows
# up (under-documented, but never silently missing).
TITLES = {
    "test_module.cpp":            "ModuleManager + SystemStatusModule",
    "test_controls.cpp":          "MoonModule control system",
    "test_reparent.cpp":          "Parent-input model (reparent / reorder)",
    "test_scheduler_affinity.cpp": "Scheduler (core affinity)",
    "test_data_buffer.cpp":       "DataBuffer / DataBufferReader (SPSC)",
    "test_pal_heap.cpp":          "Pal — heap / PSRAM",
    "test_memtracker.cpp":        "MemTracker + classSize drift",
    "test_http.cpp":              "HttpServerModule",
    "test_integration.cpp":       "REST integration (control + module API)",
    "test_state_store.cpp":       "StateStoreModule (persistence)",
    "test_preview_wire.cpp":      "PreviewModule (binary wire format)",
    "test_artnet_packing.cpp":    "ArtnetOutModule (Art-Net OpDmx)",
    "test_ripples_lut.cpp":       "RipplesEffect / PixelEffectBase",
    "test_scenarios.cpp":         "Scenario replay (whole-pipeline)",
}

CASE_RE  = re.compile(r'TEST_CASE\s*\(\s*"((?:[^"\\]|\\.)*)"')
SUB_RE   = re.compile(r'SUBCASE\s*\(\s*"((?:[^"\\]|\\.)*)"')


def main() -> int:
    files = sorted(TEST_DIR.glob("test_*.cpp"))
    if not files:
        print(f"no test files in {TEST_DIR}", file=sys.stderr)
        return 1

    lines = [
        "# What we test",
        "",
        "Every unit test grouped by the module or core surface it verifies. "
        "Generated from the `TEST_CASE` / `SUBCASE` names in "
        "`test/test_pc/*.cpp` by `scripts/build/gen_test_list.py` — a static "
        "parse, no run. Re-run that script after adding or renaming a test.",
        "",
    ]
    total = 0
    for f in files:
        src = f.read_text(encoding="utf-8", errors="replace")
        cases = CASE_RE.findall(src)
        if not cases:
            continue
        title = TITLES.get(f.name, f.name)
        lines.append(f"## {title}")
        lines.append("")
        lines.append(f"`{f.name}` — {len(cases)} test case(s)")
        lines.append("")
        # Subcases belong to the nearest preceding TEST_CASE; show them
        # nested so a parametrised case (e.g. the REST contract) reads as
        # one entry with its checks under it.
        for m in CASE_RE.finditer(src):
            name = m.group(1).replace("\\\"", '"')
            lines.append(f"- {name}")
            total += 1
            seg_end = src.find('TEST_CASE', m.end())
            seg = src[m.end():seg_end if seg_end != -1 else len(src)]
            for sm in SUB_RE.finditer(seg):
                lines.append(f"    - {sm.group(1)}")
        lines.append("")

    lines.append(f"---")
    lines.append("")
    lines.append(f"**{total} test cases across {len(files)} files.** "
                 "PC host build (`uv run scripts/build/test.py`); the same "
                 "scenario fixtures also replay over REST against a live "
                 "device via `scripts/device/scenario.py`.")
    lines.append("")

    OUT.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {OUT.relative_to(ROOT)} ({total} cases, {len(files)} files)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
