#!/usr/bin/env python3
"""Check LOC budgets per surface.

For C++ surfaces: non-blank, non-comment lines per directory or file (.h/.hpp/.cpp/.cc).
For script surfaces: non-blank, non-comment lines per file (.py).

Surface paths can be either directories (e.g. src/core) or files (e.g. src/pal/PalHttp.h).
C++ surfaces are nested-aware: counting a parent directory excludes anything that has its own
budget entry (so per-file budgets in src/pal/ do not double-count under src/pal/).

Every .h/.cpp file under src/pal/ MUST have an entry in BUDGETS — adding a new pal concern
is a moment of deliberation, not a free expansion. The check fails otherwise.

Script budgets enforce the same discipline for scripts/: a script that keeps growing without
a matching removal is doing too much. Bump the budget only with a justification comment.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent

BUDGETS = {
    "src/core": 420,                  # ModuleManager + Scheduler (excludes MoonModule.{h,cpp})
                                      # +95 Sprint 16: reparent(), sort_depth_first_(), auto_wire_(), nested reorder
                                      # Sprint 17: auto_wire_ → parent_input_idx_ + targeted relink;
                                      #   net LOC-flat (rebuild_tree_ tried then removed — heap fix)
    "src/core/MoonModule.h": 250,     # contract declarations + small inlines (Sprint 3 port)
    "src/core/MoonModule.cpp": 382,   # non-trivial method implementations (Sprint 3 port)
                                      # +1 Sprint 11 ms_per_tick; +1 Sprint 16 parent_id/is_group in getSchema

    # Pal — one budget per platform concern. New pal file → new entry here.
    # Deferred pal files (PalGpio, PalRtos, PalHeap) are intentionally absent:
    # each lands with its first consumer (Sprint 6), and the budget entry
    # lands in the same PR. Pre-registering a budget for a file that has no
    # caller is the v1 kitchen-sink pattern this list exists to prevent.
    "src/pal/Pal.h":           100,  # millis, micros, yield, sleep
    "src/pal/PalFs.h":         150,  # LittleFS (ESP32) + std::filesystem (PC) — lands in Sprint 5 for WiFi credentials
    "src/pal/PalWifi.h":       100,  # WiFi STA primitives — lands in Sprint 5 with WifiStaModule
    "src/pal/PalUdp.h":        150,  # UDP send abstraction — lands in Sprint 6 with ArtnetOutModule
    "src/pal/PalRtos.h":       100,  # xTaskCreatePinnedToCore wrapper — Sprint 7 multi-core
    "src/pal/PalHeap.h":       100,  # psram_alloc / psram_free — Sprint 7 large buffers
    "src/pal/PalHttp.h":       350,  # HTTP server abstraction (Sprint 2 port, v1 verbatim is 332)
    "src/pal/PalWs.h":         450,  # WebSocket abstraction (Sprint 3 port, v1 verbatim is 483)
    "src/pal/PalSystemInfo.h": 250,  # chip_model, reset_reason_str, build info — bumped 200→250 in Sprint 4 when the ESP32 branch landed

    "src/modules/network": 275,      # Module wrappers only (HttpServerModule + WebSocketModule)
    "src/modules/system":  600,      # SystemStatusModule + WifiStaModule + Logger + StateStoreModule + MemTracker — bumped 500→600 in Sprint 8 (Rail 2 MemTracker.h ~75 LOC)
    "src/modules/lights":  600,      # RGB.h + Pixelable.h + PixelRegistry.h + Ripples/Preview/ArtnetOut (Sprint 6)

    # Per-test budgets — Sprint 8 Rail 1 behavioral coverage. Post-write count + ~15% headroom.
    # A test that grows past its budget is doing too much — split or trim before bumping.
    "test/test_pc/test_module.cpp":             65,   # ModuleManager smoke + factory
    "test/test_pc/test_http.cpp":              150,   # HttpServerModule integration (raw TCP probe)
    "test/test_pc/test_pal_heap.cpp":           60,   # pal::psram_alloc / psram_free contract
    "test/test_pc/test_data_buffer.cpp":        115,   # DataBuffer SPSC + cross-thread tear + two-reader independence (DataBufferReader)
    "test/test_pc/test_ripples_lut.cpp":       125,   # LUT path + revision bump + hue dominance
    "test/test_pc/test_preview_wire.cpp":       70,   # Binary frame header + body byte-for-byte
    "test/test_pc/test_artnet_packing.cpp":     75,   # Art-Net OpDmx 18-byte header per universe
    "test/test_pc/test_scheduler_affinity.cpp": 75,   # coreAffinity dispatch (one-core / two-core)
    "test/test_pc/test_state_store.cpp":        85,   # /modules.json + /state-<id>.json round-trip
    "test/test_pc/test_scenarios.cpp":         130,   # Rail 3 — JSON-driven in-process pipeline replay
    "test/test_pc/test_reparent.cpp":          160,   # Sprint 17 — name-match + wildcard "source" reparent, reject-on-no-input, exact-wins-over-source, promote-keeps-value, loop order, nested reorder
}

EXTS = {".h", ".hpp", ".cpp", ".cc"}

# Script budgets — non-blank, non-comment lines per .py under scripts/
# (recursive; subfolders checks/ build/ device/). Each script stays small —
# one thing per file. Bump a budget only with a justification comment.
SCRIPT_BUDGETS = {
    # Orchestrator (scripts/ root). MoonDeck's UI is three real static files
    # in scripts/moondeck_ui/ (index.html/style.css/app.js) — not LOC-budgeted,
    # same as src/frontend/. moondeck.py is the server/orchestrator only.
    "scripts/moondeck.py":         740,   # server/orchestrator only (was a 1450 monolith;
                                          # UI → scripts/moondeck_ui/, REST orchestration
                                          # → scripts/device/light_setup.py)
    # Checks (scripts/checks/)
    "scripts/checks/check_class_sizes.py": 430,  # largest check; LOC driven by ESP32 struct table
    "scripts/checks/check_ui.py":          140,
    "scripts/checks/check_loc.py":         170,  # self-referential; bump when new surfaces land
    "scripts/checks/check_hot_path.py":     65,
    "scripts/checks/check_platform_guards.py": 65,
    "scripts/checks/check_patches.py":      60,
    "scripts/checks/classify_tests.py":     55,
    "scripts/checks/check_cppcheck.py":     45,
    "scripts/checks/check_gpio.py":         40,
    "scripts/checks/check_structure.py":    30,
    "scripts/checks/check_bundle.py":       30,
    "scripts/checks/check_ruff.py":         15,
    "scripts/checks/check_analysis.py":     15,
    # Build (scripts/build/)
    "scripts/build/gen_frontend_bundle.py": 80,
    "scripts/build/inject_build_info.py":   45,
    "scripts/build/mkdocs_serve.py":        40,
    "scripts/build/test.py":                35,
    "scripts/build/build.py":               25,
    "scripts/build/_pio.py":                20,
    "scripts/build/run.py":                 20,
    # Device (scripts/device/)
    "scripts/device/scenario.py":          200,
    "scripts/device/light_setup.py":        70,  # standalone REST orchestration
    "scripts/device/monitor.py":            35,
    "scripts/device/flash.py":              35,
}


def count_loc(path: Path, exclusions: list) -> int:
    if path.is_file():
        files = [path] if path.suffix in EXTS else []
    else:
        files = [f for f in path.rglob("*") if f.is_file() and f.suffix in EXTS]
    total = 0
    for f in files:
        if any(f == e or str(f).startswith(str(e) + "/") for e in exclusions):
            continue
        for line in f.read_text().splitlines():
            s = line.strip()
            if not s or s.startswith("//"):
                continue
            total += 1
    return total


def count_py_loc(path: Path) -> int:
    total = 0
    for line in path.read_text().splitlines():
        s = line.strip()
        if not s or s.startswith("#"):
            continue
        total += 1
    return total


def check_pal_files_have_budgets() -> bool:
    """Fail if any .h/.cpp under src/pal/ lacks a BUDGETS entry."""
    pal_dir = ROOT / "src" / "pal"
    if not pal_dir.exists():
        return True
    budgeted = {ROOT / p for p in BUDGETS if p.startswith("src/pal/")}
    ok = True
    for f in pal_dir.rglob("*"):
        if not f.is_file() or f.suffix not in EXTS:
            continue
        if f not in budgeted:
            rel = f.relative_to(ROOT)
            print(f"FAIL: pal file without BUDGETS entry: {rel}")
            print("      add to scripts/check_loc.py BUDGETS with a LOC limit "
                  "and a one-line comment describing the concern")
            ok = False
    return ok


def check_scripts_have_budgets() -> bool:
    """Fail if any .py under scripts/ (incl. subfolders) lacks a budget."""
    scripts_dir = ROOT / "scripts"
    if not scripts_dir.exists():
        return True
    budgeted = {ROOT / p for p in SCRIPT_BUDGETS}
    ok = True
    for f in sorted(scripts_dir.rglob("*.py")):
        if "__pycache__" in f.parts:
            continue
        if f not in budgeted:
            rel = f.relative_to(ROOT)
            print(f"FAIL: script without SCRIPT_BUDGETS entry: {rel}")
            print("      add to scripts/check_loc.py SCRIPT_BUDGETS with a LOC limit")
            ok = False
    return ok


def main(argv):
    failed = False
    all_paths = [ROOT / p for p in BUDGETS]
    for path_str, limit in BUDGETS.items():
        path = ROOT / path_str
        if not path.exists():
            print(f"SKIP: {path_str} (does not exist)")
            continue
        exclusions = [p for p in all_paths if p != path and str(p).startswith(str(path) + "/")]
        loc = count_loc(path, exclusions)
        status = "OK  " if loc <= limit else "OVER"
        print(f"{status} {path_str}: {loc} / {limit}")
        if loc > limit:
            failed = True
    if not check_pal_files_have_budgets():
        failed = True

    print()
    for path_str, limit in SCRIPT_BUDGETS.items():
        path = ROOT / path_str
        if not path.exists():
            print(f"SKIP: {path_str} (does not exist)")
            continue
        loc = count_py_loc(path)
        status = "OK  " if loc <= limit else "OVER"
        print(f"{status} {path_str}: {loc} / {limit}")
        if loc > limit:
            failed = True
    if not check_scripts_have_budgets():
        failed = True

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
