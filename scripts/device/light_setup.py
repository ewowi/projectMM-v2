#!/usr/bin/env python3
"""Create the reference light setup on a device via its REST API.

Adds layout/ripples/preview/artnet (idempotent — skips existing ids) then
reparents to nest ripples under layout and preview+artnet under ripples.
Sprint 17: reparent sets the parent flag on the matched input (layout→layout
by name, preview/artnet→ripples via the "source" wildcard), so wiring is
implied by the parent edges — no separate control writes.

Usage: light_setup.py <host> [port]   (port defaults to 80)
"""
import json
import sys
import urllib.request

STEPS = [
    ("/api/modules", {"type": "layout", "id": "layout-0"}, "add layout-0", "layout-0"),
    ("/api/modules", {"type": "ripples", "id": "ripples-0"}, "add ripples-0", "ripples-0"),
    ("/api/modules", {"type": "preview", "id": "preview-0"}, "add preview-0", "preview-0"),
    ("/api/modules", {"type": "artnet-out", "id": "artnet-0"}, "add artnet-0", "artnet-0"),
    ("/api/modules/reparent", {"id": "ripples-0", "parent_id": "layout-0"}, "reparent ripples-0 → layout-0", None),
    ("/api/modules/reparent", {"id": "preview-0", "parent_id": "ripples-0"}, "reparent preview-0 → ripples-0", None),
    ("/api/modules/reparent", {"id": "artnet-0", "parent_id": "ripples-0"}, "reparent artnet-0 → ripples-0", None),
]


def existing_ids(host, port, timeout=3.0):
    try:
        with urllib.request.urlopen(f"http://{host}:{port}/api/modules", timeout=timeout) as r:
            return {m.get("id") for m in json.loads(r.read().decode())}
    except Exception:
        return set()


def run(host, port=80, timeout=5.0):
    """Returns (ok, log_lines)."""
    log = []
    have = existing_ids(host, port)
    for path, payload, label, skip_id in STEPS:
        if skip_id and skip_id in have:
            log.append(f"skip {skip_id} (exists)")
            continue
        try:
            req = urllib.request.Request(
                f"http://{host}:{port}{path}", data=json.dumps(payload).encode(),
                headers={"Content-Type": "application/json"}, method="POST")
            with urllib.request.urlopen(req, timeout=timeout) as r:
                log.append(f"{label}: {r.status} {r.read().decode().strip()}")
        except Exception as e:
            log.append(f"{label} FAILED: {e}")
            return False, log
    return True, log


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    host = argv[0]
    port = int(argv[1]) if len(argv) > 1 else 80
    ok, log = run(host, port)
    for line in log:
        print(line)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
