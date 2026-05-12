#!/usr/bin/env python3
"""Check LOC budgets per surface. Non-blank lines per directory or file, .h/.hpp/.cpp/.cc only.

Surface paths can be either directories (e.g. src/core) or files (e.g. src/pal/PalHttp.h).
Surfaces are nested-aware: counting a parent directory excludes anything that has its own
budget entry (so per-file budgets in src/pal/ do not double-count under src/pal/).

Every .h/.cpp file under src/pal/ MUST have an entry in BUDGETS — adding a new pal concern
is a moment of deliberation, not a free expansion. The check fails otherwise.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

BUDGETS = {
    "src/core": 300,                  # ModuleManager + Scheduler (excludes MoonModule.{h,cpp})
    "src/core/MoonModule.h": 250,     # contract declarations + small inlines (Sprint 3 port)
    "src/core/MoonModule.cpp": 350,   # non-trivial method implementations (Sprint 3 port)

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

    "src/modules/network": 250,      # Module wrappers only (HttpServerModule + WebSocketModule)
    "src/modules/system":  600,      # SystemStatusModule + WifiStaModule + Logger + StateStoreModule + MemTracker — bumped 500→600 in Sprint 8 (Rail 2 MemTracker.h ~75 LOC)
    "src/modules/lights":  600,      # RGB.h + Pixelable.h + PixelRegistry.h + Ripples/Preview/ArtnetOut (Sprint 6)

    # Per-test budgets — Sprint 8 Rail 1 behavioral coverage. Post-write count + ~15% headroom.
    # A test that grows past its budget is doing too much — split or trim before bumping.
    "test/test_pc/test_module.cpp":             65,   # ModuleManager smoke + factory
    "test/test_pc/test_http.cpp":              150,   # HttpServerModule integration (raw TCP probe)
    "test/test_pc/test_pal_heap.cpp":           60,   # pal::psram_alloc / psram_free contract
    "test/test_pc/test_frame_ring.cpp":        110,   # SPSC ordering + cross-thread tear test
    "test/test_pc/test_ripples_lut.cpp":       125,   # LUT path + revision bump + hue dominance
    "test/test_pc/test_preview_wire.cpp":       70,   # Binary frame header + body byte-for-byte
    "test/test_pc/test_artnet_packing.cpp":     75,   # Art-Net OpDmx 18-byte header per universe
    "test/test_pc/test_scheduler_affinity.cpp": 75,   # coreAffinity dispatch (one-core / two-core)
    "test/test_pc/test_state_store.cpp":        85,   # /modules.json + /state-<id>.json round-trip
    "test/test_pc/test_scenarios.cpp":         130,   # Rail 3 — JSON-driven in-process pipeline replay
}

EXTS = {".h", ".hpp", ".cpp", ".cc"}


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
            print(f"      add to scripts/check_loc.py BUDGETS with a LOC limit "
                  f"and a one-line comment describing the concern")
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
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
