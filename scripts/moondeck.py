#!/usr/bin/env python3
"""scripts/moondeck.py — MoonDeck: local browser dev console for projectMM v2.

Usage: uv run scripts/moondeck.py [--port 8765]

Prints the URL on startup; open it in a browser manually. Four tabs:

  PC          — Build / Test / Run + all guardrail checks + docs serve
  ESP32       — env-scoped Build / Flash firmware / Flash filesystem / Monitor
  Live  — discovered Devices list + in-process and live scenario runners
  Develop — Release/Sprint dropdowns + agent-driven sprint authoring

Plus an agent loop below the output panel (Analyze / Fix / Ask via `claude -p`)
and an iframe right-pane that swaps between script output, device UIs, and
docs pages. Stdlib only, bound to 127.0.0.1.

Pre-commit and CI invoke scripts directly (see .githooks/pre-commit and
.github/workflows/ci.yml). MoonDeck is for interactive developer use only.

# PATCH: moondeck-monolith — HTTP server, HTML/CSS/JS, agent prompts, device
# discovery, and process management all live in one file for stdlib-only,
# zero-dependency deployment. Remove when the file exceeds its check_loc.py
# budget and a split pays for itself (e.g. separate moondeck_ui.py for the
# inline HTML blob, or a templated file served from disk).
"""
import argparse
import json
import os
import shutil
import signal
import subprocess
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

ROOT = Path(__file__).resolve().parent.parent

# Card catalogue. Each card declares its `tab` (pc / esp32 / live) and `group`
# within that tab. Cards on the esp32 tab opt into the tab-scoped env selector
# via `needs_env: True` (env injected as the first positional arg) and into
# the tab-scoped port selector via `needs_port: True` (appended as
# `--port <value>`). `extra_args` lands between env and port. Fixed
# positional args still work for non-dynamic cards via `args: [...]`.
ESP32_ENVS = ["esp32dev", "esp32s3_n16r8"]

SCRIPTS = [
    # ── PC tab — everything that runs locally, no ESP32 ────────────────────
    {"id": "build",        "tab": "pc",  "group": "Build & Test", "label": "Build",     "scripts": ["build.py"]},
    {"id": "test",         "tab": "pc",  "group": "Build & Test", "label": "Test",      "scripts": ["test.py"]},
    {"id": "run",          "tab": "pc",  "group": "Build & Test", "label": "Run",       "scripts": ["run.py"], "long_running": True, "url": "http://127.0.0.1:8080"},
    {"id": "all-checks",   "tab": "pc",  "group": "Checks", "label": "Run all checks", "scripts": ["check_loc.py", "check_hot_path.py", "check_gpio.py", "check_structure.py", "check_platform_guards.py", "check_bundle.py", "check_class_sizes.py"]},
    {"id": "check-loc",       "tab": "pc", "group": "Checks", "label": "LOC budgets",       "scripts": ["check_loc.py"]},
    {"id": "check-hot-path",  "tab": "pc", "group": "Checks", "label": "Hot-path bans",     "scripts": ["check_hot_path.py"]},
    {"id": "check-gpio",      "tab": "pc", "group": "Checks", "label": "Raw-GPIO ban",      "scripts": ["check_gpio.py"]},
    {"id": "check-structure", "tab": "pc", "group": "Checks", "label": "Structure",         "scripts": ["check_structure.py"]},
    {"id": "check-platform",  "tab": "pc", "group": "Checks", "label": "Platform guards",   "scripts": ["check_platform_guards.py"]},
    {"id": "check-bundle",    "tab": "pc", "group": "Checks", "label": "Frontend bundle drift", "scripts": ["check_bundle.py"]},
    {"id": "check-analysis",  "tab": "pc", "group": "Checks", "label": "Code analysis (lizard)", "scripts": ["check_analysis.py"]},
    {"id": "check-ruff",        "tab": "pc", "group": "Checks", "label": "Python lint (ruff)",              "scripts": ["check_ruff.py"]},
    {"id": "check-cppcheck",   "tab": "pc", "group": "Checks", "label": "C++ static analysis (cppcheck)",  "scripts": ["check_cppcheck.py"]},
    {"id": "check-class-sizes","tab": "pc", "group": "Checks", "label": "Class size estimates",            "scripts": ["check_class_sizes.py"]},
    {"id": "check-ui",         "tab": "pc", "group": "Checks", "label": "UI control-sync audit",           "scripts": ["check_ui.py"]},
    {"id": "check-patches",    "tab": "pc", "group": "Checks", "label": "Known patches (PATCH: markers)",   "scripts": ["check_patches.py"]},
    {"id": "gen-bundle",   "tab": "pc",  "group": "Docs", "label": "Regenerate frontend bundle", "scripts": ["gen_frontend_bundle.py"]},
    {"id": "mkdocs",       "tab": "pc",  "group": "Docs", "label": "MkDocs serve", "scripts": ["mkdocs_serve.py"], "long_running": True, "url": "http://127.0.0.1:8000"},

    # ── ESP32 tab — env + port come from tab-scoped selectors ──────────────
    {"id": "esp32-build",   "tab": "esp32", "group": "Build",          "label": "Build",            "scripts": ["build.py"],   "needs_env": True},
    {"id": "esp32-flash",   "tab": "esp32", "group": "Flash",          "label": "Flash firmware",   "scripts": ["flash.py"],   "needs_env": True, "needs_port": True},
    {"id": "esp32-flashfs", "tab": "esp32", "group": "Flash",          "label": "Flash filesystem", "scripts": ["flash.py"],   "needs_env": True, "needs_port": True, "extra_args": ["--target", "uploadfs"]},
    {"id": "esp32-monitor", "tab": "esp32", "group": "Serial monitor", "label": "Serial monitor",   "scripts": ["monitor.py"], "needs_env": True, "needs_port": True, "long_running": True},

    # ── Live tests tab ────────────────────────────────────────────────────
    {"id": "live-scenarios",         "tab": "live", "group": "Scenarios", "label": "Run scenarios (in-process via doctest)",  "scripts": ["test.py"]},
    {"id": "live-scenarios-devices", "tab": "live", "group": "Scenarios", "label": "Run scenarios (live, all enabled devices)", "scripts": ["scenario.py"], "args": ["--all-enabled"]},
]


# USB VID:PID → friendly label for the ESP32 board family attached. The bridge
# chip family is the strongest hint without actually opening the port and
# probing the chip (which would conflict with an active monitor). Unknown
# VID/PIDs fall back to pyserial's `description` string.
ESP32_USB_HINTS = {
    (0x10C4, 0xEA60): "ESP32 (CP210x)",         # Silicon Labs CP2102/CP2104 — common on DevKitC / NodeMCU-32
    (0x1A86, 0x7523): "ESP32 (CH340)",          # QinHeng CH340G — common on cheap clones
    (0x1A86, 0x55D4): "ESP32 (CH343)",          # QinHeng CH343 — newer ESP32 boards
    (0x0403, 0x6010): "ESP32 (FT2232H)",        # FTDI — JTAG + UART debug boards
    (0x0403, 0x6014): "ESP32 (FT232H)",
    (0x0403, 0x6001): "ESP32 (FT232R)",
    (0x303A, 0x1001): "ESP32-S2/S3 (USB-CDC)",  # Espressif USB-OTG native CDC
    (0x303A, 0x0002): "ESP32-S3/C3 (USB-JTAG)", # Espressif built-in USB Serial/JTAG
}


def scan_ports():
    """Enumerate USB-serial ports + their board-family label.

    Uses pyserial's serial.tools.list_ports for cross-platform VID/PID access.
    Filters to USB-only ports (macOS: /dev/cu.usb*; Linux: /dev/ttyUSB* and
    /dev/ttyACM*) so /dev/cu.debug-console and Bluetooth-RFCOMM channels don't
    pollute the picker. Returns a list of {path, label} objects sorted by path.
    """
    try:
        from serial.tools import list_ports
    except ImportError:
        return []  # pyserial missing — UI shows "(no USB serial devices)"

    out = []
    for p in list_ports.comports():
        if sys.platform == "darwin":
            if not p.device.startswith("/dev/cu.usb"):
                continue
        elif sys.platform.startswith("linux"):
            if not (p.device.startswith("/dev/ttyUSB") or p.device.startswith("/dev/ttyACM")):
                continue
        key = (p.vid, p.pid) if p.vid is not None and p.pid is not None else None
        label = ESP32_USB_HINTS.get(key) or (p.product or p.description or "")
        # pyserial's description on Linux often is just "ttyUSB0" — strip it.
        if label == p.name:
            label = ""
        out.append({"path": p.device, "label": label})
    out.sort(key=lambda x: x["path"])
    return out

# ── Release / sprint discovery for the Develop tab ────────────────────
# Parses docs/development/release-*.md for the `## Sprint N — Title {#sprint-N}`
# headings so the Develop tab's dropdowns reflect actual doc content
# rather than a hardcoded list. Re-scanned at process start; restart moondeck.py
# to pick up new releases or sprints. Minimal enough — no file watcher.

import re as _re_releases


def scan_releases():
    out = []
    dev_dir = ROOT / "docs" / "development"
    if not dev_dir.is_dir():
        return out
    for path in sorted(dev_dir.glob("release-*.md")):
        text = path.read_text(encoding="utf-8")
        title_m = _re_releases.search(r"^# (.+)$", text, _re_releases.MULTILINE)
        title = title_m.group(1).strip() if title_m else path.stem
        sprints = []
        for m in _re_releases.finditer(
            r"^## Sprint (\d+) — (.+?)\s*\{#(sprint-\d+)\}",
            text, _re_releases.MULTILINE,
        ):
            sprints.append({
                "id":    m.group(3),
                "label": f"Sprint {m.group(1)}",
                "title": m.group(2).strip(),
            })
        out.append({"slug": path.stem, "title": title, "sprints": sprints})
    return out


# ── Develop-tab agent tasks ───────────────────────────────────────────
# Each entry is a card on the Develop tab below the Release/Sprint
# selectors. Clicking Run sends the task's prompt to `claude -p` from the
# repo root; the agent uses Read/Bash/Edit to gather context (git state,
# existing docs) and returns markdown or a plan. Add a new task → add a
# `### {label} {#{anchor}}` section to docs/developer-guide/deploy.md so the `?` button
# resolves; the frontend renders one card per entry automatically.
DEV_TASKS = [
    {
        "id":           "reverse-engineer-sprint",
        "label":        "Reverse engineer sprint",
        "docs_anchor":  "reverse-engineer-sprint",
        "timeout":      600,
        "prompt": (
            "You are helping draft a new sprint section for projectMM v2's "
            "release plan, retroactively describing the work currently sitting "
            "as uncommitted changes in this repo.\n\n"
            "Steps:\n"
            "  1. Run `git status --short`, `git diff HEAD`, and "
            "`git log --oneline -10` to see what's changed.\n"
            "  2. Read the latest `docs/development/release-*.md` file to see "
            "the existing sprint format (Scope blockquote, ### Definition of "
            "Done with [x] bullets, ### Deferred).\n"
            "  3. Compose a NEW sprint section that follows that format "
            "exactly. The first line of your output MUST be the heading "
            "`## Sprint N — <Title> {#sprint-N}` (pick N as the next "
            "available sprint number in the latest release file).\n\n"
            "Constraints:\n"
            "  - Reference actual changed files by name in the DoD bullets.\n"
            "  - Honor CLAUDE.md's Minimalism principle (Rule #1): if changes "
            "removed code or budgets, note it; if they added without removing, "
            "justify it briefly in the scope blurb.\n"
            "  - Output ONLY the new markdown section, ready to paste. Do NOT "
            "edit any file. Do NOT commit. Do NOT push.\n"
            "  - If the diff is empty, say so on the first line and stop.\n"
        ),
    },
    {
        "id":           "commit-via-agent",
        "label":        "Commit via agent",
        "docs_anchor":  "commit-via-agent",
        "timeout":      600,
        "prompt": (
            "You are creating a git commit for the pending changes in this "
            "projectMM v2 repo. The user has already reviewed the changes "
            "and is asking for the commit to be made.\n\n"
            "Steps:\n"
            "  1. Run `git status --short` to see what's staged vs unstaged "
            "vs untracked.\n"
            "  2. Run `git diff --cached --stat` (if anything is staged) and "
            "`git diff HEAD --stat` for the full picture; sample the actual "
            "diff content for the bigger files.\n"
            "  3. Run `git log --oneline -10` and `git log -1 --format=%B` "
            "to learn the project's commit-message style.\n"
            "  4. Decide what to stage:\n"
            "     - If a staged set already exists, commit ONLY what's "
            "staged (respect the user's curation).\n"
            "     - If nothing is staged and the unstaged set looks like ONE "
            "coherent change, stage relevant files with explicit "
            "`git add <file>` calls.\n"
            "     - If the diff spans clearly unrelated topics, STOP and "
            "explain how the user should split it. Do not commit.\n"
            "  5. Compose a commit message matching the project style:\n"
            "     - First line: lowercase prefix (`sprint N:`, `docs:`, "
            "`feat(moondeck):`, `fix:`, etc.), em-dash separator if the title has "
            "a clarifier, descriptive title ≤ 72 chars.\n"
            "     - Body: bullets or short paragraphs explaining what changed "
            "and why. Reference files / scripts by name.\n"
            "     - Honor CLAUDE.md Minimalism (Rule #1): if changes removed "
            "code or budgets, note it; if added, say what they replace.\n"
            "     - Append a footer line: "
            "`Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>`.\n"
            "  6. Create the commit with a HEREDOC so newlines are preserved: "
            "`git commit -m \"$(cat <<'EOF'\\n…\\nEOF\\n)\"`.\n"
            "  7. After the commit succeeds, run "
            "`git log -1 --format='%h  %s'` and report the result.\n\n"
            "Constraints:\n"
            "  - Do NOT `git push`.\n"
            "  - Do NOT `git commit --amend` (always a new commit).\n"
            "  - Do NOT pass `--no-verify` (let pre-commit hooks run; if a "
            "hook fails, fix the issue, re-stage, create a NEW commit).\n"
            "  - NEVER `git add -A` / `git add .` (could include secrets); "
            "stage individual files explicitly.\n"
            "  - Skip files that look like secrets: `.env`, "
            "`credentials*.json`, `wifi.json`, `moondeck.json`.\n"
            "  - If `git status` is clean, say so on the first line and stop.\n"
        ),
    },
]


# ── moondeck.json persistence: discovered devices + subnet for Live ───────
# Stored at repo root (gitignored: dev-host-specific IPs/MACs). Frontend
# fetches via GET /ui-state on load; POSTs the full document back when the
# user toggles a checkbox, removes a device, or runs a discovery scan.
UI_STATE_PATH = ROOT / "moondeck.json"
_ui_state_lock = threading.Lock()
DEFAULT_UI_STATE = {
    "version":     1,
    "scan_subnet": "192.168.1.0/24",
    "scan_port":   80,
    "devices": [
        {"name": "PC (local Run card)", "host": "127.0.0.1", "port": 8080,
         "enabled": True, "discovered": "default"},
    ],
}


def load_ui_state():
    with _ui_state_lock:
        if not UI_STATE_PATH.exists():
            return json.loads(json.dumps(DEFAULT_UI_STATE))  # deep copy
        try:
            with open(UI_STATE_PATH, encoding="utf-8") as f:
                return json.load(f)
        except (json.JSONDecodeError, OSError):
            return json.loads(json.dumps(DEFAULT_UI_STATE))


def save_ui_state(state):
    with _ui_state_lock:
        with open(UI_STATE_PATH, "w", encoding="utf-8") as f:
            json.dump(state, f, indent=2)
            f.write("\n")


def probe_device(host, port, timeout=2.0):
    """GET http://host:port/api/system; return parsed dict or None on failure.

    Used by the Refresh button (per-device status) and by the subnet scanner
    (to confirm a hit is a projectMM v2 device, not just any HTTP server).
    The /api/system route returns a JSON blob from SystemStatusModule; its
    `chip_model` + `mac_address` + `env` fields identify the device.
    """
    import urllib.request
    try:
        url = f"http://{host}:{port}/api/system"
        with urllib.request.urlopen(url, timeout=timeout) as r:
            data = json.loads(r.read().decode())
            # Require a chip_model field so we don't claim arbitrary HTTP
            # servers (e.g. a router admin page) as projectMM devices.
            return data if isinstance(data, dict) and "chip_model" in data else None
    except Exception:
        return None


def fetch_device_modules(host, port, timeout=3.0):
    """GET http://host:port/api/modules; return list or None on failure.

    Routed through MoonDeck so the browser doesn't depend on the device
    serving CORS headers (parallel to probe_device). Caller (the Live tab's
    inline module table) polls this every 2 s while expanded.
    """
    import urllib.request
    try:
        url = f"http://{host}:{port}/api/modules"
        with urllib.request.urlopen(url, timeout=timeout) as r:
            data = json.loads(r.read().decode())
            return data if isinstance(data, list) else None
    except Exception:
        return None


def scan_subnet(subnet, port, timeout=1.5):
    """Sweep a CIDR subnet and return the projectMM v2 devices that respond."""
    import ipaddress
    from concurrent.futures import ThreadPoolExecutor
    try:
        net = ipaddress.ip_network(subnet, strict=False)
    except ValueError:
        return []
    hosts = [str(h) for h in net.hosts()]
    devices = []
    with ThreadPoolExecutor(max_workers=32) as ex:
        for h, info in zip(hosts, ex.map(lambda x: probe_device(x, port, timeout), hosts)):
            if info is None:
                continue
            mac = info.get("mac_address", "") or ""
            short = mac.replace(":", "")[-4:].upper() if mac else h
            devices.append({
                "name":       f"MM-{short}" if mac else h,
                "host":       h,
                "port":       port,
                "enabled":    True,
                "discovered": "scan",
                "chip":       info.get("chip_model", ""),
                "mac":        mac,
                "env":        info.get("env", ""),
            })
    return devices


# Long-running process registry: id -> subprocess.Popen
_running = {}
_lock = threading.Lock()


INDEX_HTML = r"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>MoonDeck — projectMM v2</title>
<link rel="icon" type="image/png" href="/assets/moonlight-logo.png">
<style>
  body { font-family: system-ui, sans-serif; margin: 0; background: #1e1e22; color: #e0e0e0; }
  header { background: #2a2a30; padding: 14px 24px; border-bottom: 1px solid #444; display: flex; align-items: center; gap: 18px; position: sticky; top: 0; z-index: 10; }
  header .logo { width: 28px; height: 28px; flex-shrink: 0; display: block; }
  header h1 { margin: 0; font-size: 1.1em; font-weight: 500; }
  .tabs { display: flex; gap: 4px; }
  .tabs .tab { background: transparent; color: #aaa; border: 1px solid #444; border-radius: 4px; padding: 4px 14px; cursor: pointer; font: inherit; font-size: 0.9em; }
  .tabs .tab:hover { color: #e0e0e0; background: #353540; }
  .tabs .tab.active { background: #3a3a55; color: #e0e0e0; border-color: #5a5a7a; }
  main { padding: 20px 24px; display: grid; grid-template-columns: 380px 1fr; gap: 24px; align-items: start; }
  .selectors { display: flex; flex-wrap: wrap; gap: 12px 16px; align-items: center; background: #232328; border: 1px solid #3a3a40; border-radius: 6px; padding: 10px 12px; margin-bottom: 12px; font-size: 0.85em; color: #aaa; }
  .selectors label { display: flex; align-items: center; gap: 6px; }
  .selectors select { background: #1e1e22; color: #e0e0e0; border: 1px solid #555; border-radius: 4px; padding: 3px 6px; font: inherit; font-size: 0.9em; min-width: 200px; }
  .selectors button { padding: 3px 8px; }
  .pane { display: none; }
  .pane.active { display: block; }
  .group { margin-bottom: 18px; }
  .group h2 { font-size: 0.75em; letter-spacing: 0.08em; text-transform: uppercase; color: #888; margin: 0 0 6px 0; }
  .card { background: #2a2a30; border: 1px solid #3a3a40; border-radius: 6px; padding: 10px 12px; margin-bottom: 6px; display: flex; align-items: center; gap: 12px; }
  .card .label { flex: 1; font-size: 0.95em; }
  .card .url { font-size: 0.78em; }
  .card .url a { color: #80cbc4; text-decoration: none; }
  .card .url a:hover { text-decoration: underline; }
  .card .help { color: #888; text-decoration: none; font-size: 0.85em; padding: 0 4px; }
  .card .help:hover { color: #ccc; }
  .card .dot { width: 10px; height: 10px; border-radius: 50%; background: #555; flex-shrink: 0; }
  .card.ok .dot { background: #4caf50; }
  .card.fail .dot { background: #f44336; }
  .card.running .dot { background: #ffa726; animation: pulse 1s ease-in-out infinite; }
  @keyframes pulse { 50% { opacity: 0.35; } }
  button { background: #3a3a42; color: #e0e0e0; border: 1px solid #555; border-radius: 4px; padding: 4px 12px; cursor: pointer; font: inherit; font-size: 0.85em; min-width: 56px; }
  button:hover:not(:disabled) { background: #4a4a52; }
  button:disabled { opacity: 0.4; cursor: not-allowed; }
  button.stop { background: #5a3636; border-color: #7a4040; }
  button.stop:hover:not(:disabled) { background: #6a4040; }
  .device-row { background: #2a2a30; border: 1px solid #3a3a40; border-radius: 6px; padding: 8px 10px; margin-bottom: 4px; display: flex; align-items: center; gap: 10px; font-size: 0.88em; }
  .device-row input[type=checkbox] { transform: scale(1.1); accent-color: #80cbc4; }
  .device-row .name { flex: 1; font-weight: 500; }
  .device-row .host { color: #999; font-family: ui-monospace, "SF Mono", Consolas, monospace; font-size: 0.85em; }
  .device-row .status { color: #888; font-size: 0.8em; min-width: 90px; text-align: right; }
  .device-row .status.ok   { color: #a5d6a7; }
  .device-row .status.fail { color: #ef9a9a; }
  .device-row .rm { background: transparent; border: none; color: #888; cursor: pointer; padding: 0 6px; min-width: auto; font-size: 1.1em; }
  .device-row .rm:hover { color: #ef9a9a; }
  .device-row .caret { width: 14px; color: #888; cursor: pointer; user-select: none; text-align: center; }
  .device-row .caret:hover { color: #ddd; }
  .device-metrics { background: #232328; border: 1px solid #3a3a40; border-radius: 6px; padding: 6px 8px; margin: -4px 0 6px 24px; font-family: ui-monospace, "SF Mono", Consolas, monospace; font-size: 0.8em; }
  .device-metrics table { width: 100%; border-collapse: collapse; }
  .device-metrics th { text-align: left; color: #888; font-weight: normal; padding: 2px 8px 2px 0; border-bottom: 1px solid #3a3a40; }
  .device-metrics td { padding: 2px 8px 2px 0; color: #ccc; }
  .device-metrics td.num { text-align: right; }
  .device-metrics .err { color: #ef9a9a; }
  .device-metrics .meta { color: #888; font-size: 0.85em; padding-top: 4px; }
  #right { display: flex; flex-direction: column; gap: 6px; min-width: 0; }
  #viewBar { display: flex; align-items: center; gap: 12px; font-size: 0.85em; color: #888; padding: 2px 4px; min-height: 22px; }
  #viewBar .mode { font-weight: 500; color: #ccc; }
  #viewBar .url a { color: #80cbc4; text-decoration: none; font-size: 0.85em; }
  #viewBar .url a:hover { text-decoration: underline; }
  #viewBar .back { background: #3a3a42; border: 1px solid #555; color: #e0e0e0; padding: 2px 10px; cursor: pointer; border-radius: 4px; font: inherit; font-size: 0.78em; }
  #viewBar .back:hover { background: #4a4a52; }
  .hidden { display: none !important; }
  #deviceFrame { width: 100%; height: calc(100vh - 140px); background: #fff; border: 1px solid #3a3a40; border-radius: 6px; }
  #output { background: #141416; border: 1px solid #3a3a40; border-radius: 6px; padding: 14px 16px; font-family: ui-monospace, "SF Mono", Consolas, monospace; font-size: 0.82em; line-height: 1.45; white-space: pre-wrap; overflow-y: auto; height: calc(100vh - 175px); margin: 0; }
  #agentBar { display: flex; gap: 8px; padding: 2px 4px; align-items: center; }
  #agentBar button { background: #2e3a4a; border-color: #4a5a72; color: #cce4ff; padding: 5px 14px; font-size: 0.85em; }
  #agentBar button:hover:not(:disabled) { background: #3a4a5e; }
  #agentBar button:disabled { opacity: 0.6; cursor: wait; }
  #agentBar button#fixBtn { background: #4a3a2e; border-color: #725a4a; color: #ffe0c8; }
  #agentBar button#fixBtn:hover:not(:disabled) { background: #5e4a3a; }
  #agentBar #askInput { flex: 1; min-width: 200px; background: #1e1e22; color: #e0e0e0; border: 1px solid #4a5a72; border-radius: 4px; padding: 5px 8px; font: inherit; font-size: 0.85em; }
  #agentBar #askInput:focus { outline: none; border-color: #6a8aa8; }
  #agentBar #askInput:disabled { opacity: 0.6; }
  #output .meta { color: #888; }
  #output .fail { color: #ef9a9a; }
  #output .ok { color: #a5d6a7; }
</style>
</head>
<body>
<header>
  <img src="/assets/moonlight-logo.png" alt="" class="logo">
  <h1>MoonDeck <span style="color:#888;font-weight:400;font-size:0.85em;">— projectMM v2</span></h1>
  <div class="tabs">
    <button class="tab" data-tab="pc">PC</button>
    <button class="tab" data-tab="esp32">ESP32</button>
    <button class="tab" data-tab="live">Live</button>
    <button class="tab" data-tab="dev">Develop</button>
  </div>
</header>
<main>
  <div id="panes">
    <div class="pane" data-tab="pc"></div>
    <div class="pane" data-tab="esp32">
      <div class="selectors">
        <label>Env <select id="envSelect"></select></label>
        <label>Port <select id="portSelect"></select></label>
        <button id="refreshPorts" title="Rescan USB serial devices">↻</button>
      </div>
    </div>
    <div class="pane" data-tab="dev">
      <div class="selectors">
        <label>Release <select id="releaseSelect"></select></label>
        <label>Sprint <select id="sprintSelect"></select></label>
        <button id="openDocsBtn" title="Open the selected sprint's section in the right panel">Documentation</button>
      </div>
      <div class="group" id="devTasksGroup">
        <h2>Sprint authoring</h2>
      </div>
    </div>
    <div class="pane" data-tab="live">
      <div class="group" id="devicesGroup">
        <h2>Devices</h2>
        <div id="deviceList"></div>
        <div class="card device-controls">
          <span class="label" id="scanLabel">Scan <code id="scanSubnetLbl">…</code></span>
          <button id="refreshDevicesBtn" title="Re-probe each known device's /api/system">Refresh</button>
          <button id="scanSubnetBtn" title="Sweep the subnet for new projectMM v2 devices">Discover</button>
        </div>
        <div class="card device-add">
          <input type="text" id="newDeviceHost" placeholder="host:port (e.g. 192.168.1.156:80)" style="flex:1;background:#1e1e22;color:#e0e0e0;border:1px solid #555;border-radius:4px;padding:3px 6px;font:inherit;font-size:0.9em;">
          <button id="addDeviceBtn">Add</button>
        </div>
      </div>
    </div>
  </div>
  <div id="right">
    <div id="viewBar">
      <span class="mode" id="viewMode">Output</span>
      <span class="url" id="viewUrl"></span>
      <button class="back hidden" id="viewBack" title="Return to script output">← Output</button>
    </div>
    <pre id="output"><span class="meta">Click Run on any script, or click a device name in the Live tab to load its UI here.</span></pre>
    <iframe id="deviceFrame" class="hidden" src="about:blank"></iframe>
    <div id="agentBar">
      <button id="analyzeBtn" title="Send the current log to Claude Code for a verdict">Analyze (by agent)</button>
      <button id="fixBtn" class="hidden" title="Ask Claude Code to fix the issue it found">Fix</button>
      <input type="text" id="askInput" placeholder="Ask the agent about this log… (Enter to send)" title="Free-form question to Claude Code, with the current log as context">
      <button id="askBtn" title="Send your question + the current log to Claude Code">Ask</button>
    </div>
  </div>
</main>
<script>
const SCRIPTS = __SCRIPTS_JSON__;
const ESP32_ENVS = __ESP32_ENVS_JSON__;
const RELEASES = __RELEASES_JSON__;
const DEV_TASKS = __DEV_TASKS_JSON__;
const DOCS_BASE = 'http://127.0.0.1:8000/projectMM-v2';
const output = document.getElementById('output');
const panes = document.querySelectorAll('.pane');
const tabs = document.querySelectorAll('.tab');
const envSelect = document.getElementById('envSelect');
const portSelect = document.getElementById('portSelect');
const refreshBtn = document.getElementById('refreshPorts');
const elById = {};   // id -> { card, button, urlSpan, eventSource, spec }

// ── Tabs ──────────────────────────────────────────────────────────────────
function showTab(name) {
  for (const t of tabs)  t.classList.toggle('active', t.dataset.tab === name);
  for (const p of panes) p.classList.toggle('active', p.dataset.tab === name);
  localStorage.setItem('tab', name);
}
for (const t of tabs) t.addEventListener('click', () => showTab(t.dataset.tab));
showTab(localStorage.getItem('tab') || 'pc');

// ── Develop tab: release + sprint dropdowns → docs iframe ─────────────
const releaseSelect = document.getElementById('releaseSelect');
const sprintSelect  = document.getElementById('sprintSelect');
const openDocsBtn   = document.getElementById('openDocsBtn');

function populateReleases() {
  releaseSelect.innerHTML = '';
  for (const r of RELEASES) {
    const opt = document.createElement('option');
    opt.value = r.slug; opt.textContent = r.title;
    releaseSelect.appendChild(opt);
  }
  const stored = localStorage.getItem('release');
  const valid  = (s) => RELEASES.find(r => r.slug === s);
  // Default: last selected, else the latest release file (the highest-numbered
  // one once more land). Minimal: no need to track "current release" anywhere
  // else; sort order of the discovery is authoritative.
  releaseSelect.value =
    valid(stored) ? stored :
    RELEASES.length ? RELEASES[RELEASES.length - 1].slug : '';
}
function populateSprints() {
  const r = RELEASES.find(x => x.slug === releaseSelect.value);
  sprintSelect.innerHTML = '';
  if (!r) return;
  for (const s of r.sprints) {
    const opt = document.createElement('option');
    opt.value = s.id;
    // Label format: "Sprint 7 — Two-core + PSRAM scaling" (truncated if long)
    const title = s.title.length > 60 ? s.title.slice(0, 57) + '…' : s.title;
    opt.textContent = `${s.label} — ${title}`;
    sprintSelect.appendChild(opt);
  }
  // Per-release sprint memory: switching releases doesn't pin the same sprint
  // number, since "Sprint 8" means different things across releases.
  const stored = localStorage.getItem(`sprint-${r.slug}`);
  const valid  = (id) => r.sprints.find(s => s.id === id);
  sprintSelect.value =
    valid(stored) ? stored :
    r.sprints.length ? r.sprints[r.sprints.length - 1].id : '';
}
releaseSelect.addEventListener('change', () => {
  localStorage.setItem('release', releaseSelect.value);
  populateSprints();
});
sprintSelect.addEventListener('change', () => {
  if (releaseSelect.value) {
    localStorage.setItem(`sprint-${releaseSelect.value}`, sprintSelect.value);
  }
});
openDocsBtn.addEventListener('click', () => {
  const slug   = releaseSelect.value;
  const sprint = sprintSelect.value;
  if (!slug || !sprint) return;
  const r = RELEASES.find(x => x.slug === slug);
  const s = r && r.sprints.find(x => x.id === sprint);
  const label = `${r ? r.title.replace(/^Release \d+ — /, 'Rel') : slug} → ${s ? s.label : sprint}`;
  showDocsUrl(`${DOCS_BASE}/development/${slug}/#${sprint}`, label);
});

populateReleases();
populateSprints();

// ── Develop tab: dev-task cards (Run invokes the agent) ───────────────
const devTasksGroup = document.getElementById('devTasksGroup');
const devTaskBtnById = {};
for (const t of DEV_TASKS) {
  const c = document.createElement('div'); c.className = 'card';
  const dot = document.createElement('span'); dot.className = 'dot';
  const lbl = document.createElement('span'); lbl.className = 'label'; lbl.textContent = t.label;
  const help = document.createElement('a');
  help.className = 'help'; help.textContent = '?';
  help.href = `${DOCS_BASE}/developer-guide/deploy/#${t.docs_anchor}`;
  help.title = `Open docs in the right panel`;
  help.addEventListener('click', (ev) => {
    ev.preventDefault();
    showDocsUrl(`${DOCS_BASE}/developer-guide/deploy/#${t.docs_anchor}`, t.label);
  });
  const btn = document.createElement('button'); btn.textContent = 'Run';
  btn.addEventListener('click', () => runDevTask(t));
  c.append(dot, lbl, help, btn);
  devTasksGroup.appendChild(c);
  devTaskBtnById[t.id] = {card: c, button: btn};
}

async function runDevTask(t) {
  const e = devTaskBtnById[t.id];
  if (!e) return;
  showOutput();
  output.textContent = '';
  append(`<span class="meta">$ ${esc(t.id)}\n</span>`);
  append(`<span class="meta">--- Agent task: ${esc(t.label)} (streaming, may take minutes) ---</span>\n`);
  e.card.className = 'card running';
  e.button.disabled = true; e.button.textContent = 'Running…';
  try {
    const res = await callAgentStream('/agent-task', {task: t.id}, appendAgentLine);
    e.card.className = 'card ' + (res.exit_code === 0 ? 'ok' : 'fail');
  } catch (err) {
    append(`<span class="fail">Task failed: ${esc(String(err))}</span>\n`);
    e.card.className = 'card fail';
  } finally {
    e.button.disabled = false; e.button.textContent = 'Run';
  }
}

// ── ESP32 selectors (env + port) ──────────────────────────────────────────
for (const env of ESP32_ENVS) {
  const opt = document.createElement('option');
  opt.value = env; opt.textContent = env;
  envSelect.appendChild(opt);
}
envSelect.value = localStorage.getItem('env') || ESP32_ENVS[0];
envSelect.addEventListener('change', () => localStorage.setItem('env', envSelect.value));
function selectedEnv()  { return envSelect.value || ''; }
function selectedPort() { return portSelect.value || ''; }

function refreshPorts() {
  return fetch('/ports').then(r => r.json()).then(({ ports }) => {
    const prev = localStorage.getItem('port') || portSelect.value;
    portSelect.innerHTML = '';
    if (ports.length === 0) {
      const opt = document.createElement('option');
      opt.value = ''; opt.textContent = '(no USB serial devices)';
      portSelect.appendChild(opt);
    } else {
      for (const p of ports) {
        const opt = document.createElement('option');
        opt.value = p.path;
        opt.textContent = p.label ? `${p.path} — ${p.label}` : p.path;
        portSelect.appendChild(opt);
      }
      if (ports.map(p => p.path).includes(prev)) portSelect.value = prev;
    }
    updateGates();
  });
}
portSelect.addEventListener('change', () => { localStorage.setItem('port', portSelect.value); updateGates(); });
refreshBtn.addEventListener('click', refreshPorts);

function updateGates() {
  // Disable Run buttons on cards that need a port when none is selected. The
  // env selector is always populated from ESP32_ENVS, so it never gates.
  const hasPort = !!selectedPort();
  for (const e of Object.values(elById)) {
    if (!e.spec.needs_port) continue;
    if (e.button.classList.contains('stop')) continue;  // don't override a running long-runner
    e.button.disabled = !hasPort;
    e.button.title = hasPort ? '' : 'select a port first';
  }
}

// ── Render cards into their tab panes, grouped by `group` ────────────────
const groupsByTab = {};
for (const s of SCRIPTS) {
  const t = s.tab || 'pc';
  (groupsByTab[t] = groupsByTab[t] || {});
  (groupsByTab[t][s.group] = groupsByTab[t][s.group] || []).push(s);
}
for (const [tabName, groups] of Object.entries(groupsByTab)) {
  const pane = document.querySelector(`.pane[data-tab="${tabName}"]`);
  if (!pane) continue;
  for (const [name, items] of Object.entries(groups)) {
    const g = document.createElement('div');
    g.className = 'group';
    const h = document.createElement('h2'); h.textContent = name; g.appendChild(h);
    for (const s of items) {
      const c = document.createElement('div');
      c.className = 'card'; c.dataset.id = s.id;
      const dot = document.createElement('span'); dot.className = 'dot';
      const lbl = document.createElement('span'); lbl.className = 'label'; lbl.textContent = s.label;
      const urlSpan = document.createElement('span'); urlSpan.className = 'url';
      const help = document.createElement('a');
      help.className = 'help'; help.textContent = '?';
      help.href = `${DOCS_BASE}/developer-guide/deploy/#${s.id}`;
      help.title = `Open docs in the right panel (middle-click → new tab)`;
      // Left-click loads docs in the iframe; preserving the href means
      // middle-click and right-click → new tab still work for power users.
      help.addEventListener('click', (ev) => { ev.preventDefault(); showDocs(s); });
      const btn = document.createElement('button'); btn.textContent = s.long_running ? 'Start' : 'Run';
      btn.onclick = () => toggle(s);
      c.append(dot, lbl, urlSpan, help, btn);
      g.appendChild(c);
      elById[s.id] = { card: c, button: btn, urlSpan, eventSource: null, spec: s };
    }
    pane.appendChild(g);
  }
}

// On load, populate ports then reflect anything currently running on the server.
refreshPorts().then(() => fetch('/status')).then(r => r.json()).then(({ running }) => {
  for (const id of running) {
    const e = elById[id]; if (!e) continue;
    e.card.className = 'card running';
    if (e.spec.long_running) {
      setRunningUI(e);
      attachStream(e, /*alreadyRunning=*/true);
    }
  }
});

// ── Live: device list (persisted to moondeck.json) ────────────────────────
const deviceList     = document.getElementById('deviceList');
const refreshDevsBtn = document.getElementById('refreshDevicesBtn');
const scanBtn        = document.getElementById('scanSubnetBtn');
const scanSubnetLbl  = document.getElementById('scanSubnetLbl');
const addDevBtn      = document.getElementById('addDeviceBtn');
const newHostInput   = document.getElementById('newDeviceHost');
let uiState = null;

function persistUiState() {
  return fetch('/ui-state', {method: 'POST', headers: {'Content-Type': 'application/json'},
                              body: JSON.stringify(uiState)});
}

function renderDevices() {
  deviceList.innerHTML = '';
  scanSubnetLbl.textContent = `${uiState.scan_subnet}  port ${uiState.scan_port}`;
  if (uiState.devices.length === 0) {
    const empty = document.createElement('div');
    empty.className = 'device-row'; empty.style.color = '#888';
    empty.textContent = '(no devices — click Discover or Add)';
    deviceList.appendChild(empty);
    return;
  }
  uiState.devices.forEach((d, idx) => {
    const row = document.createElement('div'); row.className = 'device-row';
    const cb = document.createElement('input'); cb.type = 'checkbox'; cb.checked = !!d.enabled;
    cb.onchange = () => { d.enabled = cb.checked; persistUiState(); };
    // Caret: ▸ collapsed / ▾ expanded. Toggles inline metrics table below.
    const caret = document.createElement('span'); caret.className = 'caret';
    caret.textContent = d._expanded ? '▾' : '▸';
    caret.title = 'Show module metrics (GET /api/modules every 2s)';
    caret.onclick = () => toggleMetrics(d, caret);
    const name = document.createElement('span'); name.className = 'name'; name.textContent = d.name || '(unnamed)';
    name.style.cursor = 'pointer';
    name.title = `Click to open ${d.host}:${d.port}/ in the right panel` + (d.chip || d.env ? `\n${[d.chip, d.env].filter(Boolean).join(' / ')}` : '');
    name.onclick = () => showDevice(d);
    const host = document.createElement('span'); host.className = 'host'; host.textContent = `${d.host}:${d.port}`;
    const status = document.createElement('span'); status.className = 'status';
    status.textContent = d.last_status || '';
    if (d.last_status === 'reachable') status.classList.add('ok');
    else if (d.last_status === 'unreachable') status.classList.add('fail');
    const rm = document.createElement('button'); rm.className = 'rm'; rm.textContent = '×'; rm.title = 'Remove';
    rm.onclick = () => {
      stopMetricsPoll(d);
      uiState.devices.splice(idx, 1);
      persistUiState().then(renderDevices);
    };
    row.append(caret, cb, name, host, status, rm);
    deviceList.appendChild(row);
    // If this device was expanded before re-render (e.g. checkbox toggled
    // somewhere else), recreate its metrics panel and resume polling.
    if (d._expanded) mountMetricsPanel(d);
  });
}

// Inline-metrics state lives on the device object as `_expanded` (bool),
// `_metricsEl` (the DOM panel), and `_metricsHandle` (the setInterval id).
// Kept off uiState.devices' persisted shape — these are runtime-only.
function toggleMetrics(d, caret) {
  if (d._expanded) {
    stopMetricsPoll(d);
    d._expanded = false;
    caret.textContent = '▸';
  } else {
    d._expanded = true;
    caret.textContent = '▾';
    mountMetricsPanel(d);
  }
}

function stopMetricsPoll(d) {
  if (d._metricsHandle) { clearInterval(d._metricsHandle); d._metricsHandle = null; }
  if (d._metricsEl) { d._metricsEl.remove(); d._metricsEl = null; }
}

function mountMetricsPanel(d) {
  // Locate the parent row so the panel inserts directly under it.
  const rows = Array.from(deviceList.querySelectorAll('.device-row'));
  const row = rows.find(r => r.querySelector('.host')?.textContent === `${d.host}:${d.port}`);
  if (!row) return;
  const panel = document.createElement('div'); panel.className = 'device-metrics';
  panel.innerHTML = '<span class="meta">loading…</span>';
  row.after(panel);
  d._metricsEl = panel;
  refreshMetrics(d);
  d._metricsHandle = setInterval(() => refreshMetrics(d), 2000);
}

async function refreshMetrics(d) {
  if (!d._metricsEl) return;
  try {
    const r = await fetch('/device-modules', {method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({host: d.host, port: d.port})});
    const j = await r.json();
    const mods = j.modules || [];
    if (!d._metricsEl) return;  // user may have collapsed between request + reply
    if (mods.length === 0) {
      d._metricsEl.innerHTML = '<span class="err">no response from /api/modules</span>';
      return;
    }
    const fmtKB = b => (b/1024).toFixed(b < 1024 ? 2 : 1);
    let html = '<table><thead><tr><th>module</th><th>type</th><th class="num">core</th><th class="num">ms/tick</th><th class="num">heap</th><th class="num">psram</th><th class="num">class</th></tr></thead><tbody>';
    for (const m of mods) {
      const ms = (m.ms_per_tick ?? 0).toFixed(2);
      html += `<tr><td>${esc(m.id || '')}</td><td>${esc(m.type || '')}</td>`
            + `<td class="num">${m.core ?? 0}</td>`
            + `<td class="num">${ms}</td>`
            + `<td class="num">${fmtKB(m.heap_size_bytes ?? 0)} KB</td>`
            + `<td class="num">${fmtKB(m.psram_size_bytes ?? 0)} KB</td>`
            + `<td class="num">${m.class_size_bytes ?? 0} B</td></tr>`;
    }
    html += '</tbody></table>';
    html += `<div class="meta">${mods.length} module(s) · refresh 2s · ${new Date().toLocaleTimeString()}</div>`;
    d._metricsEl.innerHTML = html;
  } catch (e) {
    if (d._metricsEl) d._metricsEl.innerHTML = `<span class="err">fetch failed: ${esc(String(e))}</span>`;
  }
}

async function probeOne(d) {
  const r = await fetch('/probe', {method: 'POST', headers: {'Content-Type': 'application/json'},
                                    body: JSON.stringify({host: d.host, port: d.port})});
  const j = await r.json();
  d.last_status = j.reachable ? 'reachable' : 'unreachable';
  if (j.reachable && j.info) {
    d.chip = j.info.chip_model || d.chip;
    d.mac  = j.info.mac_address || d.mac;
    d.env  = j.info.env || d.env;
    if (j.info.mac_address && (!d.name || /^MM-/.test(d.name))) {
      const tail = j.info.mac_address.replace(/:/g, '').slice(-4).toUpperCase();
      d.name = `MM-${tail}`;
    }
  }
}

async function refreshAllDevices() {
  refreshDevsBtn.disabled = true; refreshDevsBtn.textContent = 'Probing…';
  await Promise.all(uiState.devices.map(probeOne));
  await persistUiState();
  renderDevices();
  refreshDevsBtn.disabled = false; refreshDevsBtn.textContent = 'Refresh';
}

async function runScan() {
  scanBtn.disabled = true; scanBtn.textContent = 'Scanning…';
  try {
    const r = await fetch('/scan', {method: 'POST', headers: {'Content-Type': 'application/json'},
                                    body: JSON.stringify({subnet: uiState.scan_subnet, port: uiState.scan_port})});
    const { devices } = await r.json();
    // Merge: existing entries keep their `enabled` toggle; scan-found
    // entries are added or have their chip/mac/env refreshed.
    const byHost = new Map(uiState.devices.map(d => [d.host + ':' + d.port, d]));
    for (const found of devices) {
      const key = found.host + ':' + found.port;
      const existing = byHost.get(key);
      if (existing) {
        Object.assign(existing, {chip: found.chip, mac: found.mac, env: found.env,
                                 last_status: 'reachable', name: existing.name || found.name});
      } else {
        uiState.devices.push({...found, last_status: 'reachable'});
      }
    }
    await persistUiState();
    renderDevices();
  } finally {
    scanBtn.disabled = false; scanBtn.textContent = 'Discover';
  }
}

function addDeviceFromInput() {
  const raw = (newHostInput.value || '').trim();
  if (!raw) return;
  // host[:port] — default 80
  const m = raw.match(/^([^\s:]+)(?::(\d+))?$/);
  if (!m) { alert('Use the form host or host:port (e.g. 192.168.1.156:80)'); return; }
  const host = m[1];
  const port = m[2] ? parseInt(m[2], 10) : 80;
  if (uiState.devices.some(d => d.host === host && d.port === port)) {
    alert('Already in the list.'); return;
  }
  uiState.devices.push({name: host, host, port, enabled: true, discovered: 'manual'});
  newHostInput.value = '';
  persistUiState().then(renderDevices);
}

refreshDevsBtn.addEventListener('click', refreshAllDevices);
scanBtn.addEventListener('click', runScan);
addDevBtn.addEventListener('click', addDeviceFromInput);
newHostInput.addEventListener('keydown', ev => { if (ev.key === 'Enter') addDeviceFromInput(); });

fetch('/ui-state').then(r => r.json()).then(s => { uiState = s; renderDevices(); });

function append(html) { output.insertAdjacentHTML('beforeend', html); output.scrollTop = output.scrollHeight; }
function esc(s) { return s.replace(/[&<>]/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;'})[c]); }

// ── Right-pane view toggle: Output ↔ Device iframe ────────────────────────
const deviceFrame = document.getElementById('deviceFrame');
const viewMode    = document.getElementById('viewMode');
const viewUrl     = document.getElementById('viewUrl');
const viewBack    = document.getElementById('viewBack');
const agentBar    = document.getElementById('agentBar');
const analyzeBtn  = document.getElementById('analyzeBtn');
const fixBtn      = document.getElementById('fixBtn');
const askInput    = document.getElementById('askInput');
const askBtn      = document.getElementById('askBtn');
let   lastAnalysis = '';

function showOutput() {
  output.classList.remove('hidden');
  agentBar.classList.remove('hidden');
  deviceFrame.classList.add('hidden');
  viewBack.classList.add('hidden');
  viewMode.textContent = 'Output';
  viewUrl.innerHTML = '';
}
function showDevice(d) {
  const url = `http://${d.host}:${d.port}/`;
  // Only reload if the URL actually changed — avoids re-mount + WS reconnect
  // when the user clicks the same device twice.
  if (deviceFrame.src !== url) deviceFrame.src = url;
  output.classList.add('hidden');
  agentBar.classList.add('hidden');
  deviceFrame.classList.remove('hidden');
  viewBack.classList.remove('hidden');
  viewMode.textContent = `Device — ${d.name || (d.host + ':' + d.port)}`;
  viewUrl.innerHTML = `<a href="${url}" target="_blank" rel="noopener">open in new tab ↗</a>`;
}
function showDocsUrl(url, label) {
  // Generic docs viewer. Requires the MkDocs serve card to be running
  // (port 8000); if it's not, the iframe shows a connection-refused page
  // and the hint is self-evident.
  if (deviceFrame.src !== url) deviceFrame.src = url;
  output.classList.add('hidden');
  agentBar.classList.add('hidden');
  deviceFrame.classList.remove('hidden');
  viewBack.classList.remove('hidden');
  viewMode.textContent = `Docs — ${label}`;
  viewUrl.innerHTML = `<a href="${url}" target="_blank" rel="noopener">open in new tab ↗</a>`;
}
function showDocs(s) {
  // Card `?` click adapter.
  showDocsUrl(`${DOCS_BASE}/developer-guide/deploy/#${s.id}`, s.label);
}
viewBack.addEventListener('click', showOutput);

// ── Agent loop: Analyze / Fix / Ask — all stream via SSE so the user sees
//    the agent's narration + tool calls live (same UX as a terminal run).
async function callAgentStream(endpoint, payload, onLine) {
  const r = await fetch(endpoint, {method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify(payload)});
  if (!r.ok) {
    const text = await r.text();
    throw new Error(`${r.status}: ${text}`);
  }
  // Parse SSE off the response body manually (EventSource is GET-only).
  const reader = r.body.getReader();
  const decoder = new TextDecoder();
  let buffer = '';
  let full = '';
  let exitCode = 0;
  while (true) {
    const {value, done} = await reader.read();
    if (done) break;
    buffer += decoder.decode(value, {stream: true});
    let idx;
    while ((idx = buffer.indexOf('\n\n')) !== -1) {
      const block = buffer.slice(0, idx);
      buffer = buffer.slice(idx + 2);
      let ev = '', data = '';
      for (const ln of block.split('\n')) {
        if      (ln.startsWith('event: ')) ev = ln.slice(7);
        else if (ln.startsWith('data: '))  data += (data ? '\n' : '') + ln.slice(6);
      }
      if (ev === 'line') {
        onLine(data);
        full += data + '\n';
      } else if (ev === 'done') {
        exitCode = parseInt(data) || 0;
      }
    }
  }
  return {output: full, exit_code: exitCode};
}
function appendAgentLine(data) { append(esc(data) + '\n'); }

analyzeBtn.addEventListener('click', async () => {
  const log = output.innerText.trim();
  if (!log) { alert('Nothing in the log yet — run a script first.'); return; }
  analyzeBtn.disabled = true; analyzeBtn.textContent = 'Analyzing…';
  fixBtn.classList.add('hidden');
  append(`\n<span class="meta">--- Agent analysis ---</span>\n`);
  try {
    const res = await callAgentStream('/analyze', {log}, appendAgentLine);
    lastAnalysis = res.output || '';
    const firstLine = (lastAnalysis.split('\n', 1)[0] || '').trim();
    if (firstLine.startsWith('ISSUE')) fixBtn.classList.remove('hidden');
  } catch (e) {
    append(`<span class="fail">Analyze failed: ${esc(String(e))}</span>\n`);
  } finally {
    analyzeBtn.disabled = false; analyzeBtn.textContent = 'Analyze (by agent)';
  }
});
fixBtn.addEventListener('click', async () => {
  if (!lastAnalysis) { alert('Run Analyze first.'); return; }
  if (!confirm('The agent will edit files in this repo. Continue?')) return;
  const log = output.innerText.trim();
  fixBtn.disabled = true; fixBtn.textContent = 'Fixing…';
  append(`\n<span class="meta">--- Agent fix ---</span>\n`);
  try {
    await callAgentStream('/fix', {log, analysis: lastAnalysis}, appendAgentLine);
  } catch (e) {
    append(`<span class="fail">Fix failed: ${esc(String(e))}</span>\n`);
  } finally {
    fixBtn.disabled = false; fixBtn.textContent = 'Fix';
  }
});

async function sendAsk() {
  const prompt = askInput.value.trim();
  if (!prompt) { askInput.focus(); return; }
  const log = output.innerText.trim();
  askBtn.disabled = true; askInput.disabled = true; askBtn.textContent = 'Asking…';
  append(`\n<span class="meta">--- Ask: ${esc(prompt)} ---</span>\n`);
  try {
    await callAgentStream('/ask', {log, prompt}, appendAgentLine);
    askInput.value = '';   // clear on success; let user see the question in the log
  } catch (e) {
    append(`<span class="fail">Ask failed: ${esc(String(e))}</span>\n`);
  } finally {
    askBtn.disabled = false; askInput.disabled = false; askBtn.textContent = 'Ask';
    askInput.focus();
  }
}
askBtn.addEventListener('click', sendAsk);
askInput.addEventListener('keydown', (ev) => { if (ev.key === 'Enter' && !ev.shiftKey) { ev.preventDefault(); sendAsk(); } });

function setRunningUI(e) {
  e.button.textContent = 'Stop';
  e.button.classList.add('stop');
  e.button.disabled = false;
  if (e.spec.url) e.urlSpan.innerHTML = `<a href="${e.spec.url}" target="_blank">open ↗</a>`;
}
function setIdleUI(e, code) {
  e.button.textContent = e.spec.long_running ? 'Start' : 'Run';
  e.button.classList.remove('stop');
  e.button.disabled = false;
  e.urlSpan.innerHTML = '';
  if (code !== undefined) e.card.className = 'card ' + (code === 0 ? 'ok' : 'fail');
}

function toggle(s) {
  const e = elById[s.id];
  if (e.button.classList.contains('stop')) {
    fetch(`/stop/${encodeURIComponent(s.id)}`, { method: 'POST' });
    if (!e.eventSource) {
      // Stopping a long-runner we are no longer streaming (another script took over the panel).
      setIdleUI(e);
      e.card.className = 'card';
    }
    // Otherwise the SSE `done` event clears the UI when the kill lands.
    return;
  }
  // Close any other open client streams so the output panel shows just this run.
  // Server processes keep running; reload to re-attach to a long-running one.
  Object.values(elById).forEach(other => {
    if (other !== e && other.eventSource) { other.eventSource.close(); other.eventSource = null; }
  });
  // Disable other Run buttons only for short-running scripts (they share the output panel).
  // Long-running scripts (mkdocs serve, monitor) run in the background; leave the UI usable.
  if (!s.long_running) {
    document.querySelectorAll('button.tab').forEach(b => { /* never disable tabs */ });
    for (const e2 of Object.values(elById)) {
      if (!e2.button.classList.contains('stop')) e2.button.disabled = true;
    }
  }
  e.button.disabled = true;
  output.textContent = '';
  showOutput();  // swap back from a device iframe view, if currently shown
  append(`<span class="meta">$ ${esc(s.id)}\n</span>`);
  e.card.className = 'card running';
  if (s.long_running) setRunningUI(e);
  attachStream(e, /*alreadyRunning=*/false);
}

function attachStream(e, alreadyRunning) {
  const qs = new URLSearchParams();
  if (alreadyRunning)                       qs.set('attach', '1');
  if (e.spec.needs_env  && selectedEnv())   qs.set('env',  selectedEnv());
  if (e.spec.needs_port && selectedPort())  qs.set('port', selectedPort());
  const url = `/run/${encodeURIComponent(e.spec.id)}${qs.toString() ? '?' + qs : ''}`;
  const es = new EventSource(url);
  e.eventSource = es;
  es.addEventListener('line', ev => append(esc(ev.data) + '\n'));
  es.addEventListener('step', ev => append(`<span class="meta">--- ${esc(ev.data)} ---\n</span>`));
  es.addEventListener('done', ev => {
    const code = parseInt(ev.data);
    append(`<span class="${code === 0 ? 'ok' : 'fail'}">exit ${code}\n</span>`);
    setIdleUI(e, code);
    for (const e2 of Object.values(elById)) {
      if (!e2.button.classList.contains('stop')) e2.button.disabled = false;
    }
    updateGates();
    es.close(); e.eventSource = null;
  });
  es.onerror = () => {
    if (e.card.classList.contains('running') && !e.spec.long_running) e.card.className = 'card fail';
    setIdleUI(e);
    for (const e2 of Object.values(elById)) {
      if (!e2.button.classList.contains('stop')) e2.button.disabled = false;
    }
    updateGates();
    es.close(); e.eventSource = null;
  };
}
</script>
</body>
</html>
"""


def _find(id_):
    return next((s for s in SCRIPTS if s["id"] == id_), None)


def _kill_group(proc):
    try:
        os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
        proc.wait(timeout=3)
    except (ProcessLookupError, subprocess.TimeoutExpired):
        try:
            os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
        except ProcessLookupError:
            pass


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass  # quiet

    def do_GET(self):
        if self.path == "/":
            return self._send_index()
        if self.path == "/status":
            return self._send_status()
        if self.path == "/ports":
            return self._send_ports()
        if self.path == "/ui-state":
            return self._send_json(load_ui_state())
        if self.path.startswith("/assets/"):
            return self._send_asset(self.path[len("/assets/"):])
        if self.path.startswith("/run/"):
            parts = urlsplit(self.path)
            id_ = parts.path[len("/run/"):]
            query = parse_qs(parts.query)
            return self._send_run(id_, query)
        self.send_error(404)

    def do_POST(self):
        if self.path.startswith("/stop/"):
            return self._send_stop(self.path[len("/stop/"):])
        if self.path == "/ui-state":
            return self._save_ui_state()
        if self.path == "/probe":
            return self._do_probe()
        if self.path == "/scan":
            return self._do_scan()
        if self.path == "/device-modules":
            return self._do_device_modules()
        if self.path == "/analyze":
            return self._do_agent("analyze")
        if self.path == "/fix":
            return self._do_agent("fix")
        if self.path == "/ask":
            return self._do_agent("ask")
        if self.path == "/agent-task":
            return self._do_agent_task()
        self.send_error(404)

    def _read_json_body(self):
        n = int(self.headers.get("Content-Length", "0") or 0)
        return json.loads(self.rfile.read(n).decode()) if n > 0 else {}

    def _send_json(self, payload, status=200):
        body = json.dumps(payload).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def _save_ui_state(self):
        try:
            state = self._read_json_body()
        except (json.JSONDecodeError, ValueError):
            self.send_error(400, "invalid json")
            return
        save_ui_state(state)
        self._send_json({"ok": True})

    def _do_probe(self):
        try:
            body = self._read_json_body()
        except (json.JSONDecodeError, ValueError):
            self.send_error(400, "invalid json")
            return
        host = (body.get("host") or "").strip()
        port = int(body.get("port") or 80)
        if not host:
            self.send_error(400, "host required")
            return
        info = probe_device(host, port, timeout=2.0)
        self._send_json({"reachable": info is not None, "info": info or {}})

    def _do_scan(self):
        try:
            body = self._read_json_body()
        except (json.JSONDecodeError, ValueError):
            self.send_error(400, "invalid json")
            return
        subnet = (body.get("subnet") or "").strip()
        port   = int(body.get("port") or 80)
        if not subnet:
            self.send_error(400, "subnet required (CIDR, e.g. 192.168.1.0/24)")
            return
        devices = scan_subnet(subnet, port)
        self._send_json({"devices": devices})

    def _do_device_modules(self):
        try:
            body = self._read_json_body()
        except (json.JSONDecodeError, ValueError):
            self.send_error(400, "invalid json")
            return
        host = (body.get("host") or "").strip()
        port = int(body.get("port") or 80)
        if not host:
            self.send_error(400, "host required")
            return
        modules = fetch_device_modules(host, port, timeout=3.0)
        self._send_json({"modules": modules or []})

    # ── Streaming agent invocations (SSE) ─────────────────────────────────
    # All four entry points (analyze / fix / ask / agent-task) build a prompt
    # and hand off to `_stream_claude_to_sse`, which spawns `claude -p` and
    # forwards stdout line-by-line as SSE `line` events. The frontend reads
    # the stream via fetch + ReadableStream (POST + streaming → EventSource
    # doesn't apply since EventSource is GET-only). User-visible benefit:
    # the panel shows the agent's narration + tool calls live, exactly like
    # running `claude -p` in a terminal.

    def _stream_response_init(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("X-Accel-Buffering", "no")
        self.end_headers()

    def _stream_claude_to_sse(self, prompt, timeout):
        """Run `claude -p <prompt>` and emit each stdout line as an SSE
        `line` event. Returns the exit code. Survives a broken client by
        draining proc.stdout to keep claude unblocked.

        Buffering caveat: claude is Node.js, not Python, so PYTHONUNBUFFERED
        doesn't apply. In practice the agent emits per-event narration and
        tool-call summaries which flush as small chunks → the UI sees the
        progress in real time. If a future version starts batching, switch
        to `--output-format stream-json` and parse events here.
        """
        if shutil.which("claude") is None:
            try:
                self._event("line", "[error] claude CLI not on PATH — install Claude Code first.")
            except (BrokenPipeError, ConnectionResetError):
                pass
            return 127
        try:
            proc = subprocess.Popen(
                ["claude", "-p", prompt],
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                text=True, bufsize=1, cwd=str(ROOT),
            )
        except Exception as e:
            try:
                self._event("line", f"[error] claude invocation failed: {e}")
            except (BrokenPipeError, ConnectionResetError):
                pass
            return 1
        try:
            for line in proc.stdout:
                try:
                    self._event("line", line.rstrip("\n"))
                except (BrokenPipeError, ConnectionResetError):
                    for _ in proc.stdout:
                        pass
                    break
            return proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            try: proc.terminate()
            except Exception: pass
            try:
                self._event("line", f"[error] claude timed out after {timeout}s")
            except (BrokenPipeError, ConnectionResetError):
                pass
            return 124

    def _do_agent(self, mode):
        """Streaming invocation. Body: {log, [prompt|analysis]}.
        mode ∈ {analyze, ask, fix}. For fix mode we rely on the project's
        `.claude/settings.json` to allow Edit/Bash etc.; we never pass
        `--dangerously-skip-permissions`.
        """
        try:
            body = self._read_json_body()
        except (json.JSONDecodeError, ValueError):
            self.send_error(400, "invalid json")
            return
        log = (body.get("log") or "").strip()
        if not log:
            self.send_error(400, "log empty")
            return

        if mode == "analyze":
            prompt = (
                "You are reviewing the output of a development script run for "
                "projectMM v2. Identify any errors, warnings, or unexpected "
                "behavior in the log below.\n\n"
                "Reply with EXACTLY ONE of the following formats. The FIRST "
                "LINE is parsed by tooling — do not preface it with markdown, "
                "headers, or quotes.\n\n"
                "  OK\n"
                "  <one or two sentences explaining what you saw and why it's fine>\n"
                "\n"
                "OR\n"
                "\n"
                "  ISSUE: <one-line summary of the problem>\n"
                "  <one or two paragraphs explaining the cause and the likely fix>\n"
                "\n"
                "Log:\n"
                f"{log}\n"
            )
            timeout = 180
        elif mode == "ask":
            user_prompt = (body.get("prompt") or "").strip()
            if not user_prompt:
                self.send_error(400, "prompt empty")
                return
            prompt = (
                "You are an assistant for projectMM v2 development. The user "
                "has a question about a recent script run; answer concisely "
                "using the log as context. Do NOT enforce the OK/ISSUE format "
                "of the analyze action — answer the user's actual question.\n\n"
                f"Question: {user_prompt}\n\n"
                f"Log:\n{log}\n"
            )
            timeout = 180
        else:  # fix
            analysis = (body.get("analysis") or "").strip()
            prompt = (
                "A development script run for projectMM v2 had an issue "
                "identified by an earlier analysis. Use the available tools "
                "(Read, Edit, Bash) to fix it locally in this repo.\n\n"
                "Constraints:\n"
                "  - Apply minimal local edits only.\n"
                "  - Do NOT commit, push, or run destructive operations (rm, "
                "git reset, etc.).\n"
                "  - After making changes, summarize in one sentence what you "
                "changed and which files.\n"
                "  - If you cannot identify a safe fix, say so and stop.\n\n"
                f"Previously identified issue:\n{analysis}\n\n"
                f"Full log:\n{log}\n"
            )
            timeout = 600

        self._stream_response_init()
        try:
            rc = self._stream_claude_to_sse(prompt, timeout)
            self._event("done", str(rc))
        except (BrokenPipeError, ConnectionResetError):
            pass

    def _do_agent_task(self):
        """Streaming invocation for Develop-tab DEV_TASKS entries."""
        try:
            body = self._read_json_body()
        except (json.JSONDecodeError, ValueError):
            self.send_error(400, "invalid json")
            return
        task_id = (body.get("task") or "").strip()
        task = next((t for t in DEV_TASKS if t["id"] == task_id), None)
        if not task:
            self.send_error(404, f"unknown task: {task_id}")
            return
        self._stream_response_init()
        try:
            rc = self._stream_claude_to_sse(task["prompt"], task.get("timeout", 600))
            self._event("done", str(rc))
        except (BrokenPipeError, ConnectionResetError):
            pass

    def _send_index(self):
        # Strip server-only fields from DEV_TASKS before serialising — the
        # frontend only needs `id`, `label`, and `docs_anchor`. Keeps the
        # prompt server-side (smaller HTML, no accidental prompt-injection
        # surface from the page).
        dev_tasks_pub = [{"id": t["id"], "label": t["label"], "docs_anchor": t["docs_anchor"]}
                         for t in DEV_TASKS]
        html = (INDEX_HTML
                .replace("__SCRIPTS_JSON__",    json.dumps(SCRIPTS))
                .replace("__ESP32_ENVS_JSON__", json.dumps(ESP32_ENVS))
                .replace("__RELEASES_JSON__",   json.dumps(scan_releases()))
                .replace("__DEV_TASKS_JSON__",  json.dumps(dev_tasks_pub)))
        body = html.encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _send_status(self):
        with _lock:
            body = json.dumps({"running": list(_running.keys())}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _send_ports(self):
        body = json.dumps({"ports": scan_ports()}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    # Serve static assets from docs/assets/. Sandboxed: `Path(name).name`
    # strips any directory components so this handler can't reach outside
    # the assets tree (no `..` traversal, no symlink escape).
    _ASSET_MIME = {
        ".png": "image/png", ".jpg": "image/jpeg", ".jpeg": "image/jpeg",
        ".svg": "image/svg+xml", ".ico": "image/x-icon",
        ".gif": "image/gif", ".webp": "image/webp",
    }

    def _send_asset(self, name):
        safe = Path(name).name
        if not safe or safe.startswith("."):
            self.send_error(404); return
        path = ROOT / "docs" / "assets" / safe
        if not path.is_file():
            self.send_error(404); return
        mime = self._ASSET_MIME.get(path.suffix.lower(), "application/octet-stream")
        data = path.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "public, max-age=3600")
        self.end_headers()
        self.wfile.write(data)

    def _send_stop(self, id_):
        with _lock:
            proc = _running.get(id_)
        if proc:
            _kill_group(proc)
        self.send_response(204)
        self.end_headers()

    def _send_run(self, id_, query):
        s = _find(id_)
        if not s:
            self.send_error(404)
            return
        attaching = "attach" in query
        with _lock:
            if id_ in _running and not attaching:
                # already running; reject double-start
                self.send_error(409)
                return
        # Build the per-run arg list. Order matches what the scripts expect:
        #   [env from selector]  ← needs_env  (positional, first)
        #   [fixed args from spec]  ← args
        #   [extra_args from spec]  ← extra_args
        #   [--port <value>]  ← needs_port  (last)
        dyn = []
        if s.get("needs_env"):
            env = query.get("env", [""])[0].strip()
            if env in ESP32_ENVS:
                dyn.append(env)
        dyn.extend(s.get("extra_args", []))
        if s.get("needs_port"):
            port = query.get("port", [""])[0].strip()
            if port:
                dyn.extend(["--port", port])
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("X-Accel-Buffering", "no")
        self.end_headers()
        try:
            rc = 0
            for script in s["scripts"]:
                if len(s["scripts"]) > 1:
                    self._event("step", script)
                rc = self._run_one(id_, script, s.get("args", []) + dyn)
                if rc != 0:
                    break
            self._event("done", str(rc))
        except (BrokenPipeError, ConnectionResetError):
            pass  # client closed; process registered in _running keeps running

    def _run_one(self, id_, script_name, script_args):
        cmd = ["uv", "run", str(ROOT / "scripts" / script_name), *script_args]
        # Force the child Python to flush stdout per-line: when stdout is a
        # pipe (not a TTY), the default is block-buffering (~4 KB) and a
        # script printing one line per scenario step shows nothing in the
        # log window until completion. PYTHONUNBUFFERED=1 makes every print()
        # flush immediately. Covers scenario.py + any future scripts that
        # forget to pass `flush=True`. Existing scripts that use
        # `subprocess.call(pio …)` inherit stdio directly and aren't affected.
        env = {**os.environ, "PYTHONUNBUFFERED": "1"}
        proc = subprocess.Popen(
            cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, bufsize=1, preexec_fn=os.setsid, env=env,
        )
        with _lock:
            _running[id_] = proc
        try:
            for line in proc.stdout:
                try:
                    self._event("line", line.rstrip("\n"))
                except (BrokenPipeError, ConnectionResetError):
                    # SSE client gone; drain output to keep process unblocked
                    for _ in proc.stdout:
                        pass
                    break
            return proc.wait()
        finally:
            with _lock:
                _running.pop(id_, None)

    def _event(self, name, data):
        payload = f"event: {name}\ndata: {data}\n\n"
        self.wfile.write(payload.encode())
        self.wfile.flush()


def main(argv):
    p = argparse.ArgumentParser()
    p.add_argument("--port", type=int, default=8765)
    args = p.parse_args(argv)

    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    url = f"http://127.0.0.1:{args.port}/"
    print(f"MoonDeck: {url}  (Ctrl-C to stop)")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print()
        # Kill anything we started so children don't linger.
        with _lock:
            procs = list(_running.values())
        for proc in procs:
            _kill_group(proc)
        return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]) or 0)
