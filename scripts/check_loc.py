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
    "src/core": 300,                  # ModuleManager + Scheduler (excludes MoonModule.h)
    "src/core/MoonModule.h": 600,     # contract: lifecycle + controls + identity (Sprint 3 port)

    # Pal — one budget per platform concern. New pal file → new entry here.
    "src/pal/Pal.h":           100,  # millis, micros, yield, sleep
    "src/pal/PalGpio.h":       100,  # gpio_init / write / read (Sprint 4)
    "src/pal/PalFs.h":         200,  # filesystem (Sprint 4)
    "src/pal/PalRtos.h":       150,  # task_pin, semaphores (Sprint 4)
    "src/pal/PalHeap.h":       100,  # free_heap_bytes, max_alloc_bytes (Sprint 4)
    "src/pal/PalHttp.h":       350,  # HTTP server abstraction (Sprint 2 port, v1 verbatim is 332)
    "src/pal/PalWs.h":         450,  # WebSocket abstraction (Sprint 3 port, v1 verbatim is 483)
    "src/pal/PalSystemInfo.h": 200,  # chip_model, reset_reason_str, build info (Sprint 3)

    "src/modules/hello":   200,      # deleted at Sprint 3 close once SystemStatusModule lands
    "src/modules/network": 250,      # Module wrappers only (HttpServerModule + WebSocketModule)
    "src/modules/system":  300,      # SystemStatusModule (Sprint 3) + future NTP/WiFi modules
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
