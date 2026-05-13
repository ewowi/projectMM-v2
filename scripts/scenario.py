#!/usr/bin/env python3
"""scripts/scenario.py — replay test/test_pc/scenarios/*.json against a live device.

Promoted from Sprint 8 deferred on 2026-05-13 (see docs/develop/release-01.md
→ Artefact promotions). Same JSON fixtures the in-process test_scenarios.cpp
runs, driven over REST against a running projectMM v2 instance — PC binary
or flashed ESP32.

Usage:
    uv run scripts/scenario.py --host 192.168.1.156 --port 80
    uv run scripts/scenario.py --all-enabled            # iterate moondeck.json devices
    uv run scripts/scenario.py --all-enabled --scenario base-pipeline

Exits 0 if every target passes every scenario; 1 if any step or bound failed;
2 on usage error.
"""
import argparse
import json
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

sys.dont_write_bytecode = True

ROOT          = Path(__file__).resolve().parent.parent
SCENARIOS_DIR = ROOT / "test" / "test_pc" / "scenarios"
UI_STATE_PATH = ROOT / "moondeck.json"

# Head modules are added by main.cpp and must not be deleted between scenarios.
# Matches main.cpp's pre-state-store boot sequence.
HEAD_TYPES = {"system", "wifi-sta", "http", "ws", "state-store"}


def _http(method, host, port, path, body=None, timeout=10.0):
    """One JSON-in/JSON-out REST call. Raises urllib.error on failure."""
    url = f"http://{host}:{port}{path}"
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method=method)
    if data:
        req.add_header("Content-Type", "application/json")
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        raw = resp.read().decode()
        return json.loads(raw) if raw else {}


def cleanup_user_modules(host, port):
    """DELETE every module whose `type` is not in HEAD_TYPES.

    Uses /api/modules (the module list endpoint) — /api/system is a flat
    SystemStatus snapshot and does NOT carry the module list.
    """
    try:
        mods = _http("GET", host, port, "/api/modules")
    except Exception:
        return
    if not isinstance(mods, list):
        return
    for m in mods:
        if m.get("type") in HEAD_TYPES:
            continue
        try:
            _http("DELETE", host, port, f"/api/modules/{m['id']}")
        except Exception:
            pass


def run_scenario(host, port, path, settle_s):
    """Replay one scenario JSON. Returns True on pass, False on any failure."""
    name = path.stem
    data = json.loads(path.read_text(encoding="utf-8"))
    print(f"[scenario] {name}", flush=True)

    cleanup_user_modules(host, port)
    failed = False

    for step in data.get("steps", []):
        sname = step.get("name", step.get("op", "?"))
        op    = step.get("op", "")
        try:
            if op == "add_module":
                r = _http("POST", host, port, "/api/modules",
                          {"type": step["type"], "id": step["id"]})
            elif op == "set_control":
                r = _http("POST", host, port, "/api/control",
                          {"id": step["id"], "key": step["key"], "value": step["value"]})
            else:
                print(f"  SKIP {sname}: unknown op '{op}'", flush=True)
                continue
            if not r.get("ok", True):
                print(f"  FAIL {sname}: {r}", flush=True)
                failed = True
                break
        except Exception as e:
            print(f"  FAIL {sname}: {e}", flush=True)
            failed = True
            break

        if step.get("measure"):
            time.sleep(settle_s)
            try:
                info = _http("GET", host, port, "/api/system")
                mods = _http("GET", host, port, "/api/modules")
            except Exception as e:
                print(f"  measure {sname}: /api/system failed: {e}", flush=True)
                failed = True
                break
            metrics = {
                "modules":   len(mods) if isinstance(mods, list) else 0,
                "heap_free": info.get("heap_free_kb"),
                "psram":     info.get("psram_free_kb"),
                "fps":       info.get("system_fps"),
            }
            print(f"  measure {sname}  modules={metrics['modules']}  "
                  f"heap_free={metrics['heap_free']}KB  fps={metrics['fps']}", flush=True)

            # Bounds: only module_count.{min,max} are checked for now. The
            # JSON schema mirrors v1's so additional metrics (fps_min, etc.)
            # can earn their place per the same drift-episode rule that
            # unlocked this runner in the first place.
            bounds = step.get("bounds", {})
            mc = bounds.get("module_count", {})
            if "min" in mc and metrics["modules"] < mc["min"]:
                print(f"  BOUND_FAIL {sname}: module_count {metrics['modules']} < min {mc['min']}", flush=True)
                failed = True
            if "max" in mc and metrics["modules"] > mc["max"]:
                print(f"  BOUND_FAIL {sname}: module_count {metrics['modules']} > max {mc['max']}", flush=True)
                failed = True

    cleanup_user_modules(host, port)
    return not failed


def resolve_targets(args):
    if args.all_enabled:
        if not UI_STATE_PATH.exists():
            print("moondeck.json not found; launch scripts/moondeck.py and add/discover devices first.",
                  file=sys.stderr, flush=True)
            return None
        ui_state = json.loads(UI_STATE_PATH.read_text(encoding="utf-8"))
        targets = [d for d in ui_state.get("devices", []) if d.get("enabled")]
        if not targets:
            print("No enabled devices in moondeck.json.", file=sys.stderr, flush=True)
            return None
        return targets
    if args.host:
        return [{"name": args.host, "host": args.host, "port": args.port}]
    print("Use --host <h> [--port <p>] or --all-enabled.", file=sys.stderr, flush=True)
    return None


def main(argv):
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--host", help="Device host (e.g. 192.168.1.156). Alternative to --all-enabled.")
    p.add_argument("--port", type=int, default=80, help="Device HTTP port (default: 80)")
    p.add_argument("--all-enabled", action="store_true",
                   help="Iterate every enabled device in moondeck.json")
    p.add_argument("--scenario", default=None,
                   help="Run only this scenario stem (e.g. base-pipeline). Default: all.")
    p.add_argument("--settle", type=float, default=1.5,
                   help="Settle time in seconds after a 'measure' step (default: 1.5)")
    args = p.parse_args(argv)

    targets = resolve_targets(args)
    if targets is None:
        return 2

    scenarios = sorted(SCENARIOS_DIR.glob("*.json"))
    if args.scenario:
        scenarios = [s for s in scenarios if s.stem == args.scenario]
    if not scenarios:
        print(f"No scenarios found in {SCENARIOS_DIR}", file=sys.stderr, flush=True)
        return 1

    failed_targets = []
    for d in targets:
        host = d["host"]
        port = d.get("port", 80)
        name = d.get("name", host)
        print(f"\n==== {name}  ({host}:{port}) ====", flush=True)

        # Probe before running so unreachable targets fail fast with a clear line.
        # 8 s is generous — a fully-loaded s3 running ripples at 128×128 can
        # take a beat to service a fresh HTTP request between scheduler ticks.
        try:
            _http("GET", host, port, "/api/system", timeout=8.0)
        except Exception as e:
            print(f"  SKIP: unreachable ({e})", flush=True)
            failed_targets.append(name)
            continue

        any_fail = False
        for s in scenarios:
            if not run_scenario(host, port, s, args.settle):
                any_fail = True
        if any_fail:
            failed_targets.append(name)

    print(flush=True)
    if failed_targets:
        print(f"FAIL: {len(failed_targets)} target(s): {', '.join(failed_targets)}", flush=True)
        return 1
    print(f"PASS: all {len(targets)} target(s)", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
