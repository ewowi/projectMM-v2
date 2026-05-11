#!/usr/bin/env python3
"""scripts/ui.py — local browser UI for running project scripts.

Usage: uv run scripts/ui.py [--port 8765]

Prints the URL on startup; open it in a browser manually. Lists every script
in the SCRIPTS catalogue, runs them on click, streams stdout/stderr live via
SSE. Long-running scripts (e.g. mkdocs serve) get Start/Stop control; the UI
reflects state across reloads via /status. Stdlib only, bound to 127.0.0.1.

Pre-commit and CI invoke scripts directly (see .githooks/pre-commit and
.github/workflows/ci.yml). This UI is for interactive developer use.
"""
import argparse
import json
import os
import signal
import subprocess
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

SCRIPTS = [
    {"id": "build",           "group": "Build & Test", "label": "Build",          "scripts": ["build.py"]},
    {"id": "test",            "group": "Build & Test", "label": "Test (smoke)",   "scripts": ["test.py"]},
    {"id": "all-checks",      "group": "Checks",       "label": "Run all checks", "scripts": ["check_loc.py", "check_hot_path.py", "check_gpio.py", "check_structure.py"]},
    {"id": "check-loc",       "group": "Checks",       "label": "LOC budgets",    "scripts": ["check_loc.py"]},
    {"id": "check-hot-path",  "group": "Checks",       "label": "Hot-path bans",  "scripts": ["check_hot_path.py"]},
    {"id": "check-gpio",      "group": "Checks",       "label": "Raw-GPIO ban",   "scripts": ["check_gpio.py"]},
    {"id": "check-structure", "group": "Checks",       "label": "Structure",      "scripts": ["check_structure.py"]},
    {"id": "mkdocs",          "group": "Docs",         "label": "MkDocs serve",   "scripts": ["mkdocs_serve.py"], "long_running": True, "url": "http://127.0.0.1:8000"},
]

# Long-running process registry: id -> subprocess.Popen
_running = {}
_lock = threading.Lock()


INDEX_HTML = r"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>projectMM v2 — scripts</title>
<style>
  body { font-family: system-ui, sans-serif; margin: 0; background: #1e1e22; color: #e0e0e0; }
  header { background: #2a2a30; padding: 14px 24px; border-bottom: 1px solid #444; }
  header h1 { margin: 0; font-size: 1.1em; font-weight: 500; }
  main { padding: 20px 24px; display: grid; grid-template-columns: 340px 1fr; gap: 24px; align-items: start; }
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
  #output { background: #141416; border: 1px solid #3a3a40; border-radius: 6px; padding: 14px 16px; font-family: ui-monospace, "SF Mono", Consolas, monospace; font-size: 0.82em; line-height: 1.45; white-space: pre-wrap; overflow-y: auto; height: calc(100vh - 110px); margin: 0; }
  #output .meta { color: #888; }
  #output .fail { color: #ef9a9a; }
  #output .ok { color: #a5d6a7; }
</style>
</head>
<body>
<header><h1>projectMM v2 — scripts</h1></header>
<main>
  <div id="cards"></div>
  <pre id="output"><span class="meta">Click Run on any script.</span></pre>
</main>
<script>
const SCRIPTS = __SCRIPTS_JSON__;
const DOCS_BASE = 'http://127.0.0.1:8000/projectMM-v2';
const cards = document.getElementById('cards');
const output = document.getElementById('output');
const elById = {};   // id -> { card, button, urlSpan, eventSource }
const groups = {};

for (const s of SCRIPTS) (groups[s.group] = groups[s.group] || []).push(s);
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
    help.className = 'help'; help.textContent = '?'; help.target = '_blank';
    help.href = `${DOCS_BASE}/deploy/#${s.id}`;
    help.title = `Help: ${s.label}`;
    const btn = document.createElement('button'); btn.textContent = s.long_running ? 'Start' : 'Run';
    btn.onclick = () => toggle(s);
    c.append(dot, lbl, urlSpan, help, btn);
    g.appendChild(c);
    elById[s.id] = { card: c, button: btn, urlSpan, eventSource: null, spec: s };
  }
  cards.appendChild(g);
}

// On load, reflect anything currently running on the server.
fetch('/status').then(r => r.json()).then(({ running }) => {
  for (const id of running) {
    const e = elById[id]; if (!e) continue;
    e.card.className = 'card running';
    if (e.spec.long_running) {
      setRunningUI(e);
      attachStream(e, /*alreadyRunning=*/true);
    }
  }
});

function append(html) { output.insertAdjacentHTML('beforeend', html); output.scrollTop = output.scrollHeight; }
function esc(s) { return s.replace(/[&<>]/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;'})[c]); }

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
  // Long-running scripts (mkdocs serve) run in the background; leave the UI usable.
  if (!s.long_running) {
    document.querySelectorAll('button').forEach(b => { if (!b.classList.contains('stop')) b.disabled = true; });
  }
  e.button.disabled = true;
  output.textContent = '';
  append(`<span class="meta">$ ${esc(s.id)}\n</span>`);
  e.card.className = 'card running';
  if (s.long_running) setRunningUI(e);
  attachStream(e, /*alreadyRunning=*/false);
}

function attachStream(e, alreadyRunning) {
  const es = new EventSource(`/run/${encodeURIComponent(e.spec.id)}${alreadyRunning ? '?attach=1' : ''}`);
  e.eventSource = es;
  es.addEventListener('line', ev => append(esc(ev.data) + '\n'));
  es.addEventListener('step', ev => append(`<span class="meta">--- ${esc(ev.data)} ---\n</span>`));
  es.addEventListener('done', ev => {
    const code = parseInt(ev.data);
    append(`<span class="${code === 0 ? 'ok' : 'fail'}">exit ${code}\n</span>`);
    setIdleUI(e, code);
    document.querySelectorAll('button').forEach(b => { if (!b.classList.contains('stop')) b.disabled = false; });
    es.close(); e.eventSource = null;
  });
  es.onerror = () => {
    if (e.card.classList.contains('running') && !e.spec.long_running) e.card.className = 'card fail';
    setIdleUI(e);
    document.querySelectorAll('button').forEach(b => { if (!b.classList.contains('stop')) b.disabled = false; });
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
        if self.path.startswith("/run/"):
            return self._send_run(self.path[len("/run/"):].split("?", 1)[0])
        self.send_error(404)

    def do_POST(self):
        if self.path.startswith("/stop/"):
            return self._send_stop(self.path[len("/stop/"):])
        self.send_error(404)

    def _send_index(self):
        html = INDEX_HTML.replace("__SCRIPTS_JSON__", json.dumps(SCRIPTS))
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

    def _send_stop(self, id_):
        with _lock:
            proc = _running.get(id_)
        if proc:
            _kill_group(proc)
        self.send_response(204)
        self.end_headers()

    def _send_run(self, id_):
        s = _find(id_)
        if not s:
            self.send_error(404)
            return
        with _lock:
            if id_ in _running and "attach" not in self.path:
                # already running; reject double-start
                self.send_error(409)
                return
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
                rc = self._run_one(id_, script)
                if rc != 0:
                    break
            self._event("done", str(rc))
        except (BrokenPipeError, ConnectionResetError):
            pass  # client closed; process registered in _running keeps running

    def _run_one(self, id_, script_name):
        cmd = ["uv", "run", str(ROOT / "scripts" / script_name)]
        proc = subprocess.Popen(
            cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, bufsize=1, preexec_fn=os.setsid,
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
    print(f"projectMM v2 UI: {url}  (Ctrl-C to stop)")
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
