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

# MoonDeck is stdlib-only, zero-dependency, bound to 127.0.0.1, no build
# step. The UI is three real static files (scripts/moondeck_ui/) and the
# device REST orchestration lives in scripts/device/light_setup.py — this
# file is the HTTP server + card catalogue + agent handlers + process
# registry only. (The former "moondeck-monolith" PATCH is resolved: the
# inline HTML blob and orchestration logic that justified it were split out.)
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
SCRIPTS_DIR = ROOT / "scripts"
# The MoonDeck UI is three real static files in scripts/moondeck_ui/.
# index.html is served at / with a bootstrap <script> injected; style.css
# and app.js are served verbatim from the sandboxed /ui/ route.
UI_DIR = SCRIPTS_DIR / "moondeck_ui"


def resolve_script(name):
    """Map a bare script filename to its full path under scripts/<subdir>/.

    Scripts live in scripts/{checks,build,device}/. Card specs carry bare
    filenames; this is the single place the subfolder layout is known."""
    for sub in ("checks", "build", "device"):
        p = SCRIPTS_DIR / sub / name
        if p.exists():
            return p
    return SCRIPTS_DIR / name  # moondeck.py itself

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
        if self.path.startswith("/ui/"):
            return self._send_ui_file(self.path[len("/ui/"):])
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
        if self.path == "/device-setup":
            return self._do_device_setup()
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

    def _do_device_setup(self):
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
        # Shell out to the standalone script (same pattern as every card);
        # keeps the REST-orchestration logic out of the monolith.
        try:
            p = subprocess.run(
                [sys.executable, str(resolve_script("light_setup.py")),
                 host, str(port)],
                capture_output=True, text=True, timeout=30)
            log = (p.stdout + p.stderr).strip().splitlines()
            self._send_json({"ok": p.returncode == 0, "log": log})
        except Exception as e:
            self._send_json({"ok": False, "log": [str(e)]})

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
        # The UI is three real static files in scripts/moondeck_ui/. Read
        # index.html from disk and inject one bootstrap <script> with the
        # server→client data before app.js loads, so app.js / style.css stay
        # pure static assets (no templating). Strip server-only DEV_TASKS
        # fields — the page only needs id/label/docs_anchor (keeps the prompt
        # server-side, no prompt-injection surface from the page).
        dev_tasks_pub = [{"id": t["id"], "label": t["label"], "docs_anchor": t["docs_anchor"]}
                         for t in DEV_TASKS]
        bootstrap = "<script>window.__MOONDECK__=" + json.dumps({
            "scripts":   SCRIPTS,
            "esp32Envs": ESP32_ENVS,
            "releases":  scan_releases(),
            "devTasks":  dev_tasks_pub,
        }) + ";</script>"
        html = (UI_DIR / "index.html").read_text()
        html = html.replace("<!--__MOONDECK_BOOTSTRAP__-->", bootstrap)
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

    _UI_MIME = {".css": "text/css; charset=utf-8",
                ".js": "text/javascript; charset=utf-8",
                ".html": "text/html; charset=utf-8"}

    def _send_ui_file(self, name):
        # Serve the MoonDeck static UI from scripts/moondeck_ui/. Sandboxed:
        # Path(name).name strips any directory part so `..`/nested paths
        # cannot escape UI_DIR (same guard as _send_asset). No-store: the UI
        # is edited live during development; never cache it.
        safe = Path(name).name
        if not safe or safe.startswith("."):
            self.send_error(404); return
        path = UI_DIR / safe
        if not path.is_file() or path.suffix.lower() not in self._UI_MIME:
            self.send_error(404); return
        data = path.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", self._UI_MIME[path.suffix.lower()])
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
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
        cmd = ["uv", "run", str(resolve_script(script_name)), *script_args]
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
