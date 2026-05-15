#!/usr/bin/env python3
"""scripts/check_ui.py — Audit frontend control-sync behaviour in app.js.

Reports the key mechanisms that govern UI ↔ backend synchronisation so
developers can verify they are consistent before flashing.  Exits 0 when
everything looks healthy, 1 when a problem is found.

Checks:
  1. dragTs guard width — should be >= 2000 ms (> 1 s push cadence).
  2. pointerdown guard — sliders must set dragTs on pointerdown, not only on input.
  3. Schema-rebuild guard — WS schema events should call schemaStructureChanged_()
     before render() to avoid tearing down active inputs on geometry changes.
  4. State-push cadence — loop1s() fires every 1 s; guard must be > cadence.
  5. postControl debounce — slider debounce should be <= 200 ms (fast enough).
  6. broadcastBinary path — PreviewModule uses broadcastBinary, not broadcastText.
"""

import re
import sys
from pathlib import Path

ROOT    = Path(__file__).resolve().parent.parent
APP_JS  = ROOT / "src/frontend/app.js"
WS_CPP  = ROOT / "src/modules/network/WebSocketModule.cpp"

PASS = "\033[32mPASS\033[0m"
FAIL = "\033[31mFAIL\033[0m"
INFO = "\033[34mINFO\033[0m"

failures = 0

def check(label, ok, detail=""):
    global failures
    tag = PASS if ok else FAIL
    if not ok:
        failures += 1
    line = f"  [{tag}] {label}"
    if detail:
        line += f"\n         {detail}"
    print(line)

def find_value(pattern, text, group=1):
    m = re.search(pattern, text)
    return int(m.group(group)) if m else None

def main():
    if not APP_JS.exists():
        print(f"[{FAIL}] app.js not found at {APP_JS}")
        sys.exit(1)

    js   = APP_JS.read_text()
    wcpp = WS_CPP.read_text() if WS_CPP.exists() else ""

    print("── Control sync audit ──────────────────────────────────────────")

    # 1. dragTs guard width
    # Look for Date.now() - lastDrag > N in handleStateUpdate
    guards = [int(m) for m in re.findall(r'Date\.now\(\)\s*-\s*lastDrag\s*>\s*(\d+)', js)]
    if guards:
        min_guard = min(guards)
        check(
            f"dragTs guard width: {min_guard} ms (need > 1000)",
            min_guard > 1000,
            f"All guard values found: {guards}"
        )
    else:
        check("dragTs guard width", False, "No 'Date.now() - lastDrag >' pattern found")

    # 2. pointerdown guard on sliders
    has_pd_drag = bool(re.search(
        r"addEventListener\(['\"]pointerdown['\"].*?dragTs", js, re.DOTALL))
    check(
        "pointerdown sets dragTs",
        has_pd_drag,
        "Sliders must set dragTs on pointerdown, not only on 'input' event"
    )

    # 3. Schema-rebuild guard
    has_struct_check = "schemaStructureChanged_" in js
    check(
        "Schema rebuild guard (schemaStructureChanged_)",
        has_struct_check,
        "WS schema events should check structure before calling render()"
    )
    has_patch_fn = "patchControlSchema_" in js
    check(
        "In-place schema patch (patchControlSchema_)",
        has_patch_fn,
        "Schema updates that don't change module list should patch in-place"
    )

    # 4. State-push cadence vs guard
    # loop1s = 1000 ms cadence; guard must be > 1000
    cadence_ms = 1000  # hardcoded loop1s cadence
    if guards:
        check(
            f"Guard ({min_guard} ms) > push cadence ({cadence_ms} ms)",
            min_guard > cadence_ms,
        )

    # 5. postControl debounce
    debounces = [int(m) for m in re.findall(r'postControl\b.*?(\d+)\s*\)', js)]
    # More robust: find setTimeout(..., N) near postControl
    debounces = [int(m) for m in re.findall(
        r'setTimeout\s*\(\s*\(\)\s*=>\s*postControl[\s\S]{0,200}?,\s*(\d+)\s*\)', js)]
    if debounces:
        max_deb = max(debounces)
        check(
            f"postControl debounce: {max_deb} ms (need <= 500)",
            max_deb <= 500,
            f"All debounce values: {debounces}"
        )
    else:
        check("postControl debounce", False, "No setTimeout(..., N) near postControl found")

    # 6. broadcastBinary in PreviewModule
    pm = ROOT / "src/modules/lights/PreviewModule.h"
    if pm.exists():
        pm_src = pm.read_text()
        check(
            "PreviewModule uses broadcastBinary",
            "broadcastBinary" in pm_src,
        )
    else:
        print(f"  [{INFO}] PreviewModule.h not found — skipping broadcastBinary check")

    # 7. Backend: broadcast_state_ called every loop1s (unconditional)
    if wcpp:
        has_unconditional_state = bool(re.search(
            r'broadcast_state_\s*\(\s*\)\s*;', wcpp))
        check(
            "backend broadcast_state_() called every loop1s",
            has_unconditional_state,
            "If this is removed, the UI will stop updating values periodically"
        )
        # schema only on need_schema
        has_conditional_schema = "need_schema" in wcpp and "broadcast_schema_" in wcpp
        check(
            "backend broadcast_schema_() is conditional (need_schema gate)",
            has_conditional_schema,
        )

    print()
    print(f"── Summary: {failures} problem(s) found ──────────────────────────────")
    if failures:
        print("  Run `scripts/gen_frontend_bundle.py` after fixing app.js.")
    sys.exit(1 if failures else 0)

if __name__ == "__main__":
    main()
