# Deploy

Build, flash, and test on PC (and ESP32 from Sprint 3). The deploy pipeline target is three scripts: `build`, `flash`, `test`. Anything beyond that requires a structural-additions justification under the guardrails — see [process architecture](architecture/process.md).

## Interactive: the script UI {#interactive}

```bash
uv run scripts/ui.py        # prints http://127.0.0.1:8765 — open it in your browser
```

A small local page lists every script grouped by purpose. Click **Run** to execute one; stdout and stderr stream live to the output panel; the card's status dot turns green on exit 0, red otherwise. "Run all checks" runs all five guardrail checks in sequence and stops at the first failure.

Long-running scripts (currently: **MkDocs serve**) have **Start** / **Stop** instead of Run, plus a clickable link to their served URL while running. The UI reflects state across page reloads via `/status`, so reopening the tab shows what's already running and lets you stop it from any session. Children are killed via process group, so the full `uv run mkdocs serve` → mkdocs chain shuts down cleanly.

The UI is the developer's window into the project's processes — what scripts exist, what they do, what they output. It is the only place where script results render side-by-side.

## Non-interactive: direct invocation

Pre-commit, CI, and ad-hoc command-line use call scripts directly:

```bash
uv run scripts/build.py
uv run scripts/test.py
uv run scripts/check_loc.py
```

## Enable the pre-commit hook

```bash
git config core.hooksPath .githooks
```

The hook runs all five `check_*.py` scripts against the working tree before every commit. Same scripts CI runs. Requires [uv](https://docs.astral.sh/uv/) on PATH.

## Scripts

Every card in the UI has a `?` link that jumps to the matching section below.

### Build {#build}

Runs PlatformIO against `env:pc`: `pio run -e pc`. Produces `.pio/build/pc/program`; no-op if the binary is up to date. Source: `scripts/build.py`.

### Test {#test}

Runs the host unit and integration test suite via `pio test -e pc-test`. Uses [doctest](https://github.com/doctest/doctest) (fetched automatically by PlatformIO). Covers `ModuleManager` factory, `HelloModule` counter, and `HttpServerModule` REST handlers via direct dispatch (no sockets). Source: `scripts/test.py`.

### Run {#run}

Starts the compiled `pc` binary (long-running). Exposes the browser UI and REST API on `http://127.0.0.1:8080`. Start the Run card after a successful Build; Stop sends SIGTERM. Source: `scripts/run.py`.

### Run all checks {#all-checks}

Runs all `check_*` scripts (LOC budgets, hot-path bans, raw-GPIO ban, structure, platform guards) in sequence and stops at the first failure. Same set the pre-commit hook runs.

### LOC budgets {#check-loc}

Counts non-blank lines in each surface and compares against the budget:

| Surface | Budget | Notes |
|---|---|---|
| `src/core/` | 300 | ModuleManager + Scheduler only; excludes nested `MoonModule.h` |
| `src/core/MoonModule.h` | 600 | the contract: lifecycle + controls + identity (Sprint 3 port) |
| `src/pal/Pal.h` | 100 | timing: millis, micros, yield, sleep |
| `src/pal/PalGpio.h` | 100 | GPIO primitives (Sprint 4) |
| `src/pal/PalFs.h` | 200 | filesystem (Sprint 4) |
| `src/pal/PalRtos.h` | 150 | task_pin, semaphores (Sprint 4) |
| `src/pal/PalHeap.h` | 100 | heap query (Sprint 4) |
| `src/pal/PalHttp.h` | 350 | HTTP server (cpp-httplib on PC, ESPAsyncWebServer on ESP32) |
| `src/pal/PalWs.h` | 450 | WebSocket (POSIX sockets on PC, AsyncWebSocket on ESP32) |
| `src/pal/PalSystemInfo.h` | 200 | chip_model, reset_reason_str, build info |
| `src/modules/hello/` | 200 | deleted at Sprint 3 close once SystemStatusModule lands |
| `src/modules/network/` | 250 | Module wrappers only: `HttpServerModule` + `WebSocketModule` |
| `src/modules/system/` | 300 | SystemStatusModule (Sprint 3) + future NTP/WiFi modules |

`src/frontend/` (the SPA bundle) is not counted — it is generated data (gzipped JS/CSS as a `uint8_t` array). Authored UI sources (`index.html`, `style.css`, `app.js`) are `.html` / `.css` / `.js` so check_loc skips them too.

`src/pal/` files must each have a BUDGETS entry; `check_loc.py` fails on any unbudgeted pal file. Adding a new pal concern is therefore an explicit decision: pick a budget, write a one-line comment in `BUDGETS` saying what the file is for. This is what keeps the `pal/` directory partitioned by concern instead of becoming v1's kitchen-sink Pal.h.

Surfaces are nested-aware: counting `src/core/` excludes anything that falls under another budgeted sub-path. Fails if any surface exceeds its budget. Bumping a budget requires editing `scripts/check_loc.py` with a signed-off line in the release plan.

### Hot-path bans {#check-hot-path}

Scans `src/` for `void <Class>::loop[20ms|1s|10s]?() { ... }` method bodies and fails if any contains a banned pattern:

- Allocations — `new` / `malloc` / `psram_malloc` / `JsonDocument`
- Blocking calls — `delay` / `vTaskDelay` / `sleep` / `usleep` / `recv`

Implements the hot-path rules from [process architecture](architecture/process.md): no allocations and no blocking calls inside any `loop*()` body. Allocate in `setup()` or on a control-update event instead. Source: `scripts/check_hot_path.py`.

### Raw-GPIO ban {#check-gpio}

Fails on any GPIO call with a literal integer pin: `pinMode(5, ...)`, `digitalWrite(13, ...)`, `gpio_set_level(2, ...)`, `analogRead(34)`, and similar. Pins must come from a typed board configuration (e.g. `BoardPins::WS2812_DATA`) — the rule is about traceability of the pin number, not whether it is statically known. Source: `scripts/check_gpio.py`.

### Structure {#check-structure}

Lists tracked top-level paths (`git ls-files`) and fails on anything not in the allowlist hard-coded in `scripts/check_structure.py`. Adding a new top-level path requires editing the allowlist with a paired ADR under `docs/adr/`.

### Platform guards {#check-platform}

Scans every `.h` / `.hpp` / `.cpp` / `.cc` file under `src/` **except** files in `src/pal/`, and fails on any platform-identity gate: `#ifdef ARDUINO`, `#ifdef ESP_PLATFORM`, `#ifdef ESP32`, `#ifdef ARDUINO_ARCH_*`, `#include <Arduino.h>`, `#include <esp_*.h>`, `#include <freertos/...>`. Platform-conditional code lives only in `src/pal/` files; modules consume it through the `pal::*` interface. See [system architecture — Pal](architecture/system.md#pal--the-only-place-platform-conditionals-appear). Source: `scripts/check_platform_guards.py`.

### MkDocs serve {#mkdocs}

Starts the local documentation server at `http://127.0.0.1:8000/projectMM-v2/` via `uv run --extra docs mkdocs serve --dev-addr 127.0.0.1:8000`. Long-running: the card shows **Start** / **Stop**, plus a clickable "open ↗" link while running. Source: `scripts/mkdocs_serve.py`.
