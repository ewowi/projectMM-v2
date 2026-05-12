# Release 1 — Restart to Parity

> **Theme:** Bring v2 to parity with v1's first-boot pipeline — effect → blend → driver → preview, served over HTTP / WS with WiFi-STA, persisted to LittleFS, OTA-updatable, NTP-synced, ArtNet-capable — over six sprints. The decision to restart from v1 is documented in v1's [Release 9](https://ewowi.github.io/projectMM/development/release-09/). The contract under which this release executes is [process architecture](../architecture/process.md): frugality, guardrails, anti-drift.

---

## Release Overview

### What Release 1 delivers

A v2 codebase that runs the same first-boot pipeline as v1, implemented as modules over a small core that fits in one head. The core is `Module` + `ModuleManager` + `Scheduler` + a minimal `Pal`; networking, OTA, NTP, persistence, the HTTP / WS server, and the entire lighting domain (`RGB`, `pixelBuf`, effects, layers, layouts, modifiers) are modules. The deploy pipeline is three scripts: build, flash, test. v2's first user is lights, but lights live in `modules/lights/` and depend on the core, not the other way around.

### Frugality targets (CI-enforced)

| Surface | Target |
|---------|--------|
| Core (Module + Manager + Scheduler) | ≤ 300 LOC |
| Pal | ≤ 200 LOC |
| Per module (light domain) | ≤ 200 LOC |
| Per module (networking, OTA) | ≤ 300 LOC |
| Tests after Sprint 2 | ≤ 5 files |
| Deploy scripts after Sprint 1 | 3 (build, flash, test) |

Overshoot fails CI. Bumping a target requires an explicit line in this release plan signed off by the maintainer.

### Concurrency model (fixed at Sprint 1)

Arbitrary DAG topology, SPSC lock-free ring buffer per edge, depth 2 by default. The linear pipeline (effect → blend → driver) and v1's double-buffering are special cases. Depth >2 enables backpressure pipelining where memory allows.

### Recurring evaluation cadence

Release 5 is the first recurring evaluation release (per the [process architecture](../architecture/process.md): every fifth release is a no-feature evaluation). It is scheduled here, in Release 1 Sprint 1, not at the end of Release 4.

### Mid-release pivot: port-and-frugalize (2026-05-12)

Sprint 2 and Sprint 3 were initially attempted as greenfield rewrites of v1's HTTP server, WebSocket server, and frontend. The result was buggy distillations of code v1 had already debugged: TCP fragmentation, WebSocket handshake corner cases, threading races between the scheduler and HTTP handlers — all rediscovered, all fixed by reading v1's solutions after the fact. The first attempts were deleted; the sprints below are rewritten around *porting* v1's working code and frugalizing it. The discipline is codified in [process architecture §4](../architecture/process.md#4-port-and-frugalize--where-substantive-modules-come-from). Surviving artefacts from the first attempt: guardrails framework, `Module` / `ModuleManager` / `Scheduler` skeleton, `HelloModule`, factory, doctest harness, the test files covering the core and `HelloModule`.

---

## Sprints

| Sprint | Goal | Frugality target |
|--------|------|------------------|
| [Sprint 1](#sprint-1) | Guardrails framework + empty Module/Manager/Scheduler/Pal skeleton + Linux PC CI green | core ≤ 300 LOC |
| [Sprint 2](#sprint-2) | Port `HttpServer` + v1 frontend bundle; UI shell visible at `:8080` (disconnected indicator OK) | network ≤ 500 LOC |
| [Sprint 3](#sprint-3) | `Module` → `MoonModule` (merge controls); port `WsServer` + frontend sources + `SystemStatusModule`; UI shows live system status | MoonModule ≤ 600, system ≤ 300 LOC |
| [Sprint 4](#sprint-4) | Pal-minimum + ESP32 builds green + on-target tests + HIL | Pal ≤ 200 LOC |
| [Sprint 5](#sprint-5) | WiFi-STA + REST + WebSocket over hardware; frontend connects unchanged | per-module ≤ 300 LOC |
| [Sprint 6](#sprint-6) | Light domain foundation + LittleFS state persistence: RipplesEffect + Preview + Art-Net out + modules.json / per-module state survive reboot | per-module ≤ 300 LOC |
| [Sprint 7](#sprint-7) | Two-core + PSRAM scaling: PalRtos pinned tasks, PalHeap PSRAM, FrameRing SPSC, ArtnetOut pinned to core 1, stress-test 128×128 on s3 | per-module ≤ 300 LOC |

Each sprint closes with a working device (or working PC application for sprints 2–3). After Sprint 7, Release 1 is tagged `v1.0.0-foundation` — the v2 codebase has a real light pipeline, scaled persistence, and a stress-tested dual-core path on hardware. The v1 → v2 cutover (rename + final stable tag) closes [Release 2](release-02.md), which adds ArtNet **in**, OTA, NTP, and any remaining v1 parity bits.

---

## Sprint 1 — Guardrails and skeleton {#sprint-1}

> **Scope:** Land the minimum guardrails framework that the empty `Module` / `ModuleManager` / `Scheduler` / `Pal` skeleton justifies — no more. The four lifecycle cadences (`loop`, `loop20ms`, `loop1s`, `loop10s`) are first-class scheduler concerns from commit 1, not afterthoughts. Linux-PC CI green; macOS/Windows PC and ESP32 envs land when those platforms gain real code (Sprint 3).

The framework is the load-bearing deliverable. Without it, the same drift that produced v1 reaches v2 by Sprint 4. [Process architecture](../architecture/process.md) is the spec; the first commit of this sprint is its implementation. `loop20ms()` is new in v2 (v1 had only `runLoop1s` via timing windows); the scheduler maintains four cadences per core (hot, 20 ms, 1 s, 10 s) and modules opt into each by overriding the corresponding method. Empty overrides cost nothing.

`scripts/ui.py` renders the project's process surface as a browser-based control panel from day one — see [Deploy → Interactive UI](../deploy.md#interactive) and [process architecture §2](../architecture/process.md). Pre-commit and CI invoke scripts directly (non-interactive contexts).

### Definition of Done

- [x] `.gitignore`, `platformio.ini` in place (CMake deferred — PlatformIO covers both PC and ESP32)
- [x] Pre-commit hook installed (`git config core.hooksPath .githooks`): raw-GPIO ban, hot-path allocation ban, hot-path blocking-call ban, structural-additions allowlist, LOC budget
- [x] CI workflow green on Linux PC (macOS + Windows PC, esp32dev, esp32s3_n16r8 deferred to Sprint 3)
- [x] CI gates enforced: LOC budget, structural-file allowlist, hot-path bans, raw-GPIO ban
- [x] `Module` base class declares `setup` / `loop` / `loop20ms` / `loop1s` / `loop10s` / `teardown` (empty defaults)
- [x] `Scheduler` runs no-op ticks on two threads with four cadences wired (`loop` / `loop20ms` / `loop1s` / `loop10s`); pinning deferred until ESP32 where it matters
- [x] `ModuleManager` adds / removes a no-op module via API
- [x] `Pal` skeleton has timing only (`millis`, `micros`, `sleep`, `yield`)
- [x] Core total ≤ 300 LOC; verified by `scripts/check_loc.py`
- [x] Developer control panel: `scripts/ui.py` — see [Deploy → Interactive UI](../deploy.md#interactive); per-script docs at [Deploy → Scripts](../deploy.md#scripts)
- [x] Release 5 evaluation sprint scheduled (see Release Overview above)

Deferred to Sprint 2+ as "earn its place":

- Footprint baseline, test-count baseline, doc-growth budget number — no data yet
- Drift-metrics status doc — nothing to measure on an empty skeleton
- Formatters / linters / static analysers (clang-format, ruff, clang-tidy, cppcheck) — see "To be validated during Release 1" below

---

## Sprint 2 — Port `HttpServer` + serve v1 UI shell {#sprint-2}

> **Pivot:** A first pass at this sprint built `HttpServerModule` from scratch and ran into the classes of bugs (TCP fragmentation, threading races, HTTP body parsing edge cases) that v1's `core/HttpServer.h` has already debugged into stability. The sprint was reset: substantive modules in v2 are *ported* from v1, then frugalized. See [process architecture §4](../architecture/process.md#4-port-and-frugalize--where-substantive-modules-come-from). Survivors of the first attempt: guardrails framework, core skeleton, `HelloModule`, `ModuleManager` factory, doctest harness.

> **Scope:** Port v1's `src/core/HttpServer.h` (332 LOC) — landing at `src/pal/PalHttp.h` because it is a platform-conditional header wrapping `cpp-httplib`/`ESPAsyncWebServer` — and v1's `src/frontend/frontend_bundle.h` (the gzipped SPA). Frugalize `PalHttp.h`. Wrap it as a platform-neutral `HttpServerModule` (no `#ifdef`s) that serves `/` from the bundle and exposes minimal REST so v1's UI shell loads and renders. The bundle's WebSocket reconnect logic shows a "disconnected" indicator until Sprint 3 lands the WS module — that is the expected end-of-Sprint-2 visual state. PC only — ESP32 envs land in Sprint 4 but `PalHttp.h`'s `#ifdef ARDUINO` branch is kept intact so it compiles when ESP32 arrives.

### Definition of Done

- [x] `HelloModule` — `Module` subclass with counter (`loop1s`) and enabled flag; `serialize_json` override
- [x] Module factory in `ModuleManager`: `register_type` + `std::function` registry; `add(type, id)` dispatches through it (no `strcmp` chain); `find(id)` added
- [x] Test-count baseline dropped — counting test *files* adds friction without catching real drift; LOC budgets and the structural allowlist are sufficient
- [x] Vendor cpp-httplib v0.18.5 in `lib/httplib/src/` (header-only) — see [ADR 0001](../adr/0001-vendor-cpp-httplib.md); `lib` added to structural allowlist
- [x] Port v1's `HttpServer.h` verbatim into `src/pal/PalHttp.h` (port-step, no modifications) — lives in `pal/` because it contains the only platform conditional in v2; module code that uses HTTP gets the abstraction, not the conditional
- [x] New guardrail `scripts/check_platform_guards.py` rejects `#ifdef ARDUINO` / `#include <Arduino.h>` / ESP-IDF headers outside `src/pal/`; wired into pre-commit + CI + ui.py
- [x] **Frugalize** `PalHttp.h` under the §4 rule (strip patches, not features): read line by line, looking for retries / swallow-everything try/catch / timer band-aids / re-init paths. Result: **no patches over symptoms found**; LOC unchanged from v1 verbatim (332). Examined and kept:
  - 4× `catch (std::bad_alloc&)` blocks in ESP32 handlers (return 503 instead of crash) — graceful API boundary for ESP32's tight heap; a deliberate architecture decision, not a patch.
  - `catch (...)` around `listen_after_bind()` in PC branch — broader than ideal, but logs to stderr rather than swallowing silently. Acceptable.
  - Trailing `/.+` → `/*` wildcard adaptation in `onDelete`/`onPatch` — cross-platform syntax adapter between cpp-httplib regex and ESPAsyncWebServer glob; deliberate.
  - `std::map<request, body>` buffering for ESP32 `onPost`/`onPatch` — required by ESPAsyncWebServer's chunked-body API; not a patch.
  - `onPostBinary` + `BinaryChunkFn`/`BinaryEndFn` — kept for Sprint 7 (`FirmwareUpdateModule` OTA upload). Future-needed feature; per §4, not a subtraction target.
- [x] `HttpServerModule` (in `src/modules/network/`) wraps `pal::HttpServer`, registers `GET /api/modules` (list — matching v1's route set; v1 has no GET-by-id and the WebSocket schema covers per-module reads). Mutations (`POST` / `DELETE` / `PATCH`) land in Sprint 3 with the control system. Contains **zero** platform conditionals.
- [x] Port v1's `src/frontend/frontend_bundle.h` verbatim into `src/frontend/frontend_bundle.h` (generated data — not counted by LOC checks)
- [x] `HttpServerModule` serves the bundle on `GET /` via `pal::HttpServer::onGetStaticGzip` with `Content-Encoding: gzip`
- [x] **End-of-sprint verification:** open `http://127.0.0.1:8080`, see v1's UI render, see modules list (just `hello-0`), see the WebSocket-disconnected indicator (because Sprint 3 hasn't landed yet)
- [x] LOC budgets in range: `src/pal/PalHttp.h` 247 / 350 (v1 verbatim 332; frugalize was empty — no patches found), `src/modules/network/` 41 / 250
- [x] Host unit + integration tests via doctest: three `HttpServerModule` cases covering `GET /`, `GET /api/modules`, and unknown-route 404. Probe uses a 30-line raw-TCP helper rather than `httplib::Client` — cpp-httplib's Client returns `"Failed to read connection"` when used in the same process as its Server on macOS (production code is Server-only and works fine; reason captured in `test_http.cpp`).

---

## Sprint 3 — `MoonModule` + `WsServer` + `SystemStatusModule` + frontend sources {#sprint-3}

> **Scope:** Three substantive ports land together because they validate each other: the control system (lifecycle in v2's `Module` + control mechanism from v1's `StatefulModule`, merged into a single `MoonModule` class), the WebSocket transport (`PalWs.h` + `WebSocketModule`), and a real module that exercises both (`SystemStatusModule`). The v1 frontend *sources* (HTML/CSS/JS + bundle generator) come in too, frugalized to render only what v2 ships. After this sprint the UI shows live system status, controls are interactive, and the bundle can be regenerated from sources in-tree.

### Definition of Done

#### Step 1: `MoonModule` (merge `Module` + v1's `StatefulModule`)

- [x] Rename v2's `src/core/Module.h` → `src/core/MoonModule.h`; `pmm::Module` → `pmm::MoonModule` across the tree (HelloModule, HttpServerModule, ModuleManager, Scheduler, tests). `ModuleManager` keeps its name (it manages `MoonModule` instances; the asymmetry is honest).
- [x] Add ArduinoJson via `lib_deps` (ADR 0002 codifies registry-vs-vendor rule)
- [x] Merge: v1's control system (`addControl` × 10, `setControl`, `getSchema`, `getControlValues`, `clearControls`, pending props, state persistence, children, autowire hooks, `fillSystemJson`) merged into `MoonModule`. v2's tiered cadences kept (`loop20ms`/`loop1s`/`loop10s` — v1 has fewer). CRTP `StatefulModule<Derived>` wrapper dropped (single class).
- [x] **Frugalize** the merger under §4 — five refinements landed beyond a 1:1 port:
  1. Factory-injected `classSize` via `ModuleManager::register_type<T, Args...>()` — captures `sizeof(T)` once, modules write zero per-class boilerplate (replaces v1's CRTP).
  2. `onAllocateMemory()` generalizes v1's `onSizeChanged` (was lighting-only) to a no-args reallocation hook.
  3. `onBuildControls()` replaces v1's `rebuildControls()` duplication — modules put all `addControl()` calls here, framework calls it from `runSetup()` and external code calls it directly for rebuilds.
  4. `dynamicMemorySize()` derived from a single cached value the module sets in its `onAllocateMemory()`. No separate `heapSize()` to drift; PSRAM/heap not split (when PSRAM is present it's used by default; reporters care about the total).
  5. Single-method public API per concern — no `rebuild*()` wrappers; the `on*()` methods are the sole entry points. Modules pay one line of bookkeeping in the override (set `moduleAllocBytes_`; call `clearControls()` if supporting rebuild) instead of two methods per concern in the API.
- [x] **Split** into `src/core/MoonModule.h` (declarations + small inlines, 122 LOC) + `src/core/MoonModule.cpp` (substantive bodies, 328 LOC). Consistent with v2's existing pattern (ModuleManager and Scheduler are also split). Total 450 LOC vs v1's 875 inline (51%).
- [x] LOC budgets: `src/core/MoonModule.h` ≤ 250 (122 actual), `src/core/MoonModule.cpp` ≤ 350 (328 actual). `src/core/` ≤ 300 for ModuleManager + Scheduler only (excludes nested MoonModule.{h,cpp}).

#### Step 2: WebSocket transport (`PalWs.h` + `WebSocketModule`)

- [x] Port v1's `src/core/WsServer.h` into `src/pal/PalWs.h` (483 LOC → 247, frugalized). ESP32 branch: deferred frame-buffer pre-allocation infrastructure + heap_caps_get_largest_free_block guards (Sprint 4, when PalHeap lands); deferred broadcastLog (Sprint 5+, when v2 has logging). PC branch: inlined the POSIX socket calls instead of carrying v1's `PcSocketShims.h` (Windows path dropped — v2 targets Linux/macOS PC + ESP32).
- [x] Wrap as `WebSocketModule` in `src/modules/network/` — platform-neutral, no `#ifdef`s; owns a `pal::WsServer`, broadcasts schema + state JSON each `loop1s` when there are connected clients. **Deviation from initial plan**: WS lives on its own port (81), not as a `/ws` upgrade on the HTTP port. cpp-httplib has no WebSocket support and adding HTTP-upgrade handling would force a second HTTP library — v1's two-port pattern is cleaner here. Frontend connects to `ws://host:81/`.
- [x] REST mutations on `HttpServerModule`: `POST /api/modules` (add by type+id), `DELETE /api/modules/{id}` (remove), `PATCH /api/modules/{id}` (set controls — body is a JSON object of `{key: value, ...}`, each key dispatched through `setControl`). All hold `manager_->mutex()` across find+mutate to avoid races with the Scheduler.
- [x] **Bug uncovered + fixed during Step 2**: `ModuleManager::add()` was calling `m->setup()` directly instead of `m->runSetup()`. This skipped the framework's auto-registration of the `enabled_` control and any other onBuildControls work — meaning `setControl("enabled", false)` silently returned false because the control wasn't registered. Both `add()` and `remove()` now use `runSetup()` / `runTeardown()` dispatch wrappers; `Scheduler::core_loop` correspondingly uses `runLoop` / `runLoop20ms` / etc. so child recursion + enabled-gating in the dispatch wrappers actually fire.
- [x] **End-of-step verification**: HTTP REST exercised by curl — POST adds, PATCH `{"enabled":false}` stops `HelloModule::counter_` from incrementing (visible in subsequent GET), DELETE removes; raw WS probe sees the 101 handshake and receives schema + state frames at ~2/sec (matching `loop1s`).
- [x] LOC: `src/pal/PalWs.h` 247 / 450 (v1 verbatim was 483; ~51% size); `src/modules/network` 156 / 250 (HttpServerModule + WebSocketModule).

#### Step 3: `SystemStatusModule` (the first real `MoonModule`)

- [x] `src/pal/PalSystemInfo.h` ports v1's system-info `pal::*` accessors as a v2 pal-domain file. **PC stubs** for now — `chip_model="pc"`, `mac_address=""`, `total_heap_kb=0`, `cpu_cores=hardware_concurrency`, `local_time_str` via `localtime_r+strftime`, etc. Real ESP32 implementations land in Sprint 4 under `#ifdef ARDUINO` HERE in pal/, never in the modules. 48 / 200 LOC.
- [x] `SystemStatusModule` ported from v1's 198-LOC `SystemStatus.h`. Inherits `MoonModule` (not v1's `StatefulModule<Derived>` — Step 1c's factory-injected `classSize` replaced the CRTP layer). All 28 `addControl(...)` calls moved from `setup()` into `onBuildControls()` per Step 1c refinement #3 — `setup()` does the one-time hardware reads (`chip_model`, totals); `loop1s()` samples dynamic fields (heap, temp, time, fps). **Zero platform conditionals** in the module — enforced by `check_platform_guards.py`. 147 / 300 LOC.
- [x] `main.cpp` swaps `mm.add("hello", ...)` → `mm.add("system", "system-0")`; `src/modules/hello/HelloModule.h` and `test/test_pc/test_hello.cpp` deleted (mandatory subtraction for Sprint 3 close). `test_http.cpp` + `test_module.cpp` updated to use `SystemStatusModule` as the test fixture.
- [x] **Wire-format alignment with v1's frontend** (uncovered when the browser saw no controls): v2's `WebSocketModule` was emitting `{"event":"schema",...}` + a wrapped state envelope `{"event":"state","modules":[...]}` with flat key:value entries — but v1's `app.js` dispatches on `msg.t === 'schema'` and expects state as a **raw top-level array** `[{id, controls:{...}}, ...]`. The reinvention had no justification. v2 now emits v1's exact wire format: schema is `{"t":"schema","modules":[...]}`, state is a raw array with each entry `{id, controls:{key:value, ...}}`. Port-and-frugalize default applied — use v1's working design, don't reinvent.
- [x] **End-of-step verification**: open WebSocket on `:81`, see `system-0` schema with 28 controls (uptime_s, fps, local_time, heap_*, psram_*, fs_*, chip_model, mac_address, firmware_version, build_date, build_time, cpu_*, flash_*, reset_reason). State frames show `uptime_s` and `local_time` advancing each second. Browser hits `http://127.0.0.1:8080`, sees v1 UI render system status live with all 28 controls.

#### Step 4: Frontend sources

- [x] Ported v1's `src/frontend/index.html` (84 LOC), `style.css` (744 LOC), `app.js` (1647 LOC) verbatim — byte-identical to v1.
- [x] Ported `scripts/gen_frontend_bundle.py` (v1's PIO pre-script handling dropped — v2 generates on-demand, not as part of the PlatformIO build). Generator made **deterministic** by setting `gzip mtime=0`; v1's version embedded the current timestamp, so two regenerations produced different bytes — that prevented any meaningful drift check. v2's drift check now works because identical sources always produce identical bundles.
- [x] `scripts/check_bundle.py` regenerates the bundle in-memory and diffs against the committed `frontend_bundle.h`. Wired into pre-commit + CI + ui.py. Drift between sources and bundle fails CI.
- [x] `scripts/ui.py` gains two cards: "Frontend bundle drift" (runs check_bundle) and "Regenerate frontend bundle" (runs gen).
- [x] **Frugalize the frontend** under §4: the deliberation yields **nothing stripped**. The §4 rule is "future-needed features stay" — and the v1 frontend's lighting UI is needed by Sprint 6, WiFi UI by Sprint 5, firmware-upload UI by Sprint 7. All three sprints land in this release. Stripping any of it now would only mean re-porting later — exactly the waste §4 exists to prevent. The earlier Sprint 3 DoD bullet that proposed stripping "UI for features v2 does not yet ship" contradicted §4 and was wrong; the frontend is left as v1 ships it. Real UI changes happen *when* those sprints add their own features and need their own frontend bits.
- [x] **End-of-sprint verification:** open `http://127.0.0.1:8080`, see live SystemStatusModule with ticking uptime, heap, fps, local_time; controls render from schema; WS "connected" indicator green. Bundle is 24422 bytes gzipped (same compressed size as v1; bytes differ only in the gzip header timestamp now being deterministic at zero).
- [x] In-flight fixes surfaced by the end-of-sprint browser test: (a) Add-module picker was empty — frontend `GET /api/types` had no handler. Added `ModuleManager::registered_types()` and the matching `HttpServerModule` route returning `[{name, category}]`. (b) Module cards showed title "undefined" — `getSchema` emitted only `id`/`type` but the frontend reads `name`. Added `name = type` in `getSchema` (no separate human label until a module needs one). (c) Drag-and-drop reorder did nothing — frontend posts to `/api/modules/reorder` which had no handler. Added `ModuleManager::reorder(ids)` (matched ids first, unmatched modules appended in original relative order, unknown ids ignored) and the matching `HttpServerModule` route.

---

## Sprint 4 — ESP32 build envs + PalSystemInfo on hardware {#sprint-4}

> **Scope:** Prove that "every platform conditional has lived in `src/pal/` all along" — the load-bearing claim Sprints 2 + 3 made. Add `esp32dev` and `esp32s3_n16r8` build envs, wire `ESPAsyncWebServer` to those envs only, and confirm the existing pal files (`PalHttp.h`, `PalWs.h`, `PalSystemInfo.h`) compile clean on ESP32 without touching anything outside `src/pal/`. Light up `PalSystemInfo.h`'s ESP32 branch with real values. Verify on a connected device.
>
> **Scope deliberately reduced from the original draft.** The first draft DoD called for four new pal files (`PalGpio.h`, `PalFs.h`, `PalRtos.h`, `PalHeap.h`) and a typed board config codegen. None of those have a consumer yet in v2: WiFi credentials need `PalFs` (Sprint 5); the lighting driver needs `PalGpio` + `PalRtos` (Sprint 6). Landing pal files ahead of their first caller is exactly the v1 anti-pattern CLAUDE.md Rule #1 forbids — each addition must pay for itself. Each pal file is therefore deferred to the sprint that introduces its first consumer, where the budget, the implementation, and the test arrive together.

### Definition of Done

- [x] `ESP32Async/ESPAsyncWebServer` added to `lib_deps` for ESP32 envs only (already required by `PalHttp.h`'s ESP32 branch). LDF mode tuned from `chain+` to plain `chain` because chain+ pulled WiFi.cpp into the build before its sibling Network library was on CPPPATH — a known pioarduino 55 trap that v1 worked around with a `add_network_path.py` pre-script; plain `chain` sidesteps the trap entirely with one fewer moving part.
- [x] `esp32dev` and `esp32s3_n16r8` envs added to `platformio.ini`; both builds green; PC build still green; CI matrix runs all three. v1's 4 MB partition table (`partitions/esp32dev.csv`) is carried over verbatim — OTA + LittleFS + coredump slots are sized now so Sprint 5 (LittleFS) and Sprint 7 (OTA) land without reflashing a fresh layout. `partitions/` is added to `check_structure.py`'s allowlist.
- [x] `PalSystemInfo.h`'s ESP32 branch lights up via ESP-IDF + arduino-esp32 calls: `chip_model`, `mac_address`, heap/PSRAM totals, `reset_reason_str`, `sketch_kb`, flash chip details, `cpu_freq_mhz`, `platform_version`, `sdk_version`. The PC stubs stay for everything that has no PC analogue. Budget bumped 200→250 (signed off here) to fit the ESP32 branch; PC-only Sprint 3 placeholder was 86 LOC.
- [x] `check_platform_guards.py` still passes — every new `#ifdef ARDUINO` lives in `src/pal/` files. Sprint 4 added two new pal entry points hidden behind that interface: `pal::log_init(baud)` (PC: `setbuf(stdout, nullptr)`; ESP32: `Serial.begin + delay`) and `pal::on_interrupt(handler)` (PC: SIGINT; ESP32: no-op — arduino-esp32 newlib lacks `signal()`).
- [x] `main.cpp` refactored to define `setup()` + `loop()` + `int main()` on both platforms without a single `#ifdef`. Each platform's loader picks the entry point that applies: arduino-esp32's `loopTask` calls `setup()`; PC's `int main()` does the same. `setup()` blocks in `Scheduler::run()` until shutdown on both platforms, so `loop()` is never invoked.
- [x] HIL probe: `esp32dev` flashed to `/dev/cu.usbserial-20213431`; serial output verified at 115200 — device boots, `SystemStatusModule` constructs (heap/PSRAM/chip queries executed without crash), HTTP and WS modules log a deferred message and skip `AsyncServer::begin()` (their listeners cannot start before lwIP has a netif — see "Deferred" below), Scheduler enters both core loops. CI skips this step without hardware.
- [x] `scripts/ui.py` gains a USB-port picker in the header (auto-populates from `/dev/cu.usb*` / `/dev/ttyUSB*` + `/dev/ttyACM*`, persisted via `localStorage`) and six ESP32 cards: Build esp32dev / esp32s3_n16r8, Flash esp32dev / esp32s3_n16r8 (consume the picker), Serial monitor for each env (long-running, consume the picker). `scripts/flash.py` and `scripts/monitor.py` are the underlying CLIs; CI doesn't use them (no hardware). `scripts/_pio.py` resolves the right `pio` binary — prefers `~/.platformio/penv/bin/pio` (PlatformIO's bundled Python 3.11) over a Homebrew shim that may resolve to a Python 3.12 with a system `fatfs` package whose API doesn't match the espressif32 platform's expectations (causes `ImportError: cannot import name 'create_extended_partition' from 'fatfs'` at build start). Falls back to PATH lookup when the penv isn't present (CI containers install PlatformIO via pip).

### Deferred (frugality)

- [ ] **HTTP + WebSocket listener startup on ESP32** — `pal::HttpServer::begin()` / `pal::WsServer::begin()` no-op on Arduino because `AsyncServer::begin()` asserts `xQueueSemaphoreTake` when the lwIP TCP/IP task is not running, and that task only starts once WiFi or Ethernet brings up a netif. Sprint 5's `WifiStaModule` will signal network-ready and re-invoke `begin()`. The route registrations (`onGet`, `onPost`, `onPatch`, `onDelete`) still fire in module `setup()` so Sprint 5 only needs to add the trigger, not re-wire any routes.
- [ ] `PalFs.h` — lands in Sprint 5 with `WifiStaModule` (credentials persistence). `fs_total_kb` / `fs_used_kb` return 0 from `PalSystemInfo.h` on both branches until then.
- [ ] `PalGpio.h` + `PalRtos.h` + typed board-config codegen — land in Sprint 6 with the lighting driver (pin + FreeRTOS task pin).
- [ ] `PalHeap.h` — folded into `PalSystemInfo.h` for now; promoted to its own file only when a second caller appears.
- [ ] On-target unit tests — promoted from Sprint 4 to the first sprint that has platform-divergent behaviour worth asserting on hardware (likely Sprint 6's pixel buffer).

---

## Sprint 5 — WiFi-STA + REST + WebSocket over hardware {#sprint-5}

> **Scope:** `WifiStaModule` connects on boot. The Sprint 3 HTTP / REST / WebSocket stack runs over WiFi on the device. The Sprint 3 frontend connects to v2 hardware unchanged — same wire format, no frontend changes.

### Definition of Done

- [x] `PalFs.h` lands (LittleFS on ESP32, std::filesystem on PC under a `state/` sandbox). 72 / 150 LOC. `pio run -t uploadfs` writes `data/` to LittleFS; `data/wifi.json` is gitignored and matched by a committed `wifi.json.example`. `data/` added to `check_structure.py`'s allowlist.
- [x] `PalWifi.h` (59 / 100 LOC) wraps WiFi-STA primitives: `wifi_begin`, `wifi_is_connected`, `wifi_disconnect`, `wifi_local_ip`, `wifi_rssi`, `wifi_tx_power_dbm`, `wifi_set_tx_power(float dBm)`. PC stubs report disconnected so modules stay platform-neutral.
- [x] `WifiStaModule` (152 LOC) reads `/wifi.json` via PalFs on boot, calls `pal::wifi_begin`, polls `wifi_is_connected` in `loop1s()`, exposes `ssid` / `password` / `status` / `ip` / `rssi_dbm` / `tx_power_dbm` as controls. Editing ssid/password in the UI rewrites `/wifi.json` and reconnects.
- [x] **Smart TX-power adaptation.** First connect attempt at default 19.5 dBm. On 15 s timeout, step down through 17 → 15 → 13 → 11 → 8.5 dBm (table in `WifiStaModule::kTxSteps`) and retry immediately at each step — finds the *highest* working power instead of jumping to the floor. Once connected at reduced power, a probe every hour briefly raises back to default and watches for 30 s; if the link survives, we declare conditions improved and clear the reduction. If it disassociates, we restore the reduced level until the next probe. This replaces the original build-time `-DWIFI_LOLIN_FIX` flag — the runtime detection handles Lolin antenna issues, marginal USB current, and transient RF crowding without per-board configuration.
- [x] `pal::HttpServer::begin()` and `pal::WsServer::begin()` are now **idempotent** — first call after WiFi comes up starts the listener, subsequent calls return immediately. `HttpServerModule::loop1s()` and `WebSocketModule::loop1s()` invoke them every second; the listener self-starts once the netif is up. Sprint 4's "deferred" path is gone.
- [x] `/api/system` aggregates `MoonModule::fillSystemJson()` from every module (SystemStatusModule contributes heap/PSRAM/chip/temp; WifiStaModule could add later). `/api/log` returns the last 64 lines from a new `pmm::Logger` ring buffer in `src/modules/system/Logger.h`. App-level printfs in `main.cpp` and `WifiStaModule` migrated to `pmm::log()` so they land in `/api/log`; ad-hoc debug printfs (Scheduler, ModuleManager) stay on stdout only.
- [x] **Scheduler ESP32 fix.** Verified that `std::thread` on arduino-esp32 maps to pthread with a ~3 KB default stack — too small for `Scheduler::core_loop`'s mutex + JSON dispatch, which triggered a `Core 1 panic'ed (Double exception)` immediately on entry. Fix: `Scheduler::run()` runs core 0 *inline on the calling thread* (loopTask has 8 KB) and only spawns `(cores - 1)` extra `std::thread`s. `pal::default_scheduler_cores()` returns 1 on ESP32 and 2 on PC. Multi-core ESP32 lands in Sprint 6 with PalRtos's `xTaskCreatePinnedToCore` (correct stack sizing per task).
- [x] HIL verification on `/dev/cu.usbserial-20213431`: `[wifi] connected ssid=*** ip=192.168.1.234 rssi=-56 tx=19.5dBm`, HTTP listening on 8080, WS listening on 81, WS client connected. Live REST verified over WiFi from the host: `/api/types` returns 4 types, `/api/modules` returns 4 ids, `/api/system` returns real ESP32 fields (chip `ESP32-D0WD-V3 Rev 301`, MAC `24:DC:C3:A0:C1:BC`, free heap 186 KB, core temp 57 °C, env `esp32dev`), `/api/log` returns the boot sequence. `BUILD_TARGET=$PIOENV` define wired into `esp32_common` so `env=` reads the env name instead of falling back to the PC default.
- [x] Per-module footprint: `WifiStaModule` 152 LOC; `SystemStatusModule` 192 LOC; `HttpServerModule` 162 LOC; `WebSocketModule` 65 LOC — all well under the 300 LOC bound.
- [x] `pal::default_http_port()` → 80 on ESP32 (so users open `http://<device-ip>` with no port suffix), 8080 on PC (port < 1024 needs root and `run.py` is unprivileged). `main.cpp` reads from the helper; the per-instance port stays overridable via the `HttpServerModule` constructor.
- [x] Module-card stats line: `class_size_bytes`, `heap_size_bytes`, `psram_size_bytes`, `setup_ok`, `core` added to `MoonModule::getSchema()` and the `/api/modules` GET (which now serialises full schema via ArduinoJson instead of the hand-rolled `{id,type}` fallback). `MoonModule::runLoop()` increments a tick counter that `runLoop1s()` samples to compute `msPerTick_`; `WebSocketModule::broadcast_state_` emits `timing.ms_per_tick` + `timing.self_ms_per_tick` per module so the UI's fps/ms toggle has live data. Sprint 5 leaves `setup_ok=true` / `core=0` / `psram=0` as honest placeholders — real failure tracking, per-core pinning, and PSRAM-backed allocations land in Sprint 6+.

---

## Sprint 6 — Light domain foundation + state persistence {#sprint-6}

> **Scope:** Three lighting modules — `RipplesEffect` produces an RGB buffer, `PreviewModule` ships it to the frontend as a binary WS frame, `ArtnetOutModule` packs Art-Net (OpDmx) over UDP. All on the single inline core 0 from Sprint 5; cross-module sharing via a tiny `PixelRegistry`. Pixel buffers in internal heap (≤ 64×64×1 ≈ 12 KB); two-core + PSRAM scaling lands in Sprint 7.
>
> Plus **LittleFS state persistence** so the device survives reboots: a `StateStoreModule` reads `/modules.json` + `/state/<id>.json` on boot to rebuild the user's module configuration and per-control values; saves the same on add / remove / control change. Without this, a Sprint 6 device loses its lighting modules every flash — clearly insufficient for daily use.
>
> Frugality first: **no parent modules**, **no producer / consumer base classes**, **no SPSC ring**, **no PSRAM allocator**, **no per-module core affinity**, **no effect layering**, **no FastLED / WS2812 driver**. The data flow lives in three lighting modules + four headers in `modules/lights/`, one new pal file, one new state-store module. `src/core/` is untouched — the StateStoreModule walks `manager_` from the outside and uses the existing `MoonModule::saveState` / `loadState` / `getControlValues` API.

### Definition of Done

- [x] `modules/lights/RGB.h` — `struct RGB { r, g, b }` + inline `black()` and `fromHsv(h,s,v)`. 39 LOC. `static_assert(sizeof(RGB) == 3)` so consumers can `memcpy` straight into Art-Net DMX bytes and the preview frame body.
- [x] `modules/lights/Pixelable.h` — `PixelBufferRef { data, width, height, depth, revision }` + `PixelSource` abstract base. 35 LOC. The `w/h/d` shape lines up with the frontend's `renderPixelFrame` so 1D, 2D, and 3D buffers are all the same type.
- [x] `modules/lights/PixelRegistry.h` — singleton with `publish(id, src)` / `unpublish(src)` / `find(id)`. 41 LOC. Sidesteps `-fno-rtti` cleanly; no core changes, no virtual on `MoonModule`.
- [x] `modules/lights/RipplesEffect.h` — extends `MoonModule` + `PixelSource`. Controls `width` (1..64, default 16), `height` (1..64, default 16), `depth` (1..16, default 1), `speed` (default 1.0), `hue_base` (default 0.6). Allocates via `new RGB[w*h*d]` in `onAllocateMemory()`; any dimension change rebuilds the buffer and bumps `revision_++`. 2D radial-ripple pattern (`cos(distance·0.6 − t·2)` brightness, hue rotates with distance for visible bands). Logs `[ripples] allocated WxHxD = N bytes` on each rebuild.
- [x] `modules/lights/PreviewModule.h` — control `source` (default `ripples-0`). Resolves source via `PixelRegistry` and `ws-0` via `manager_->find()` + a `type()=="ws"` check + `static_cast<WebSocketModule*>` (no RTTI). In `loop20ms()` packs `[0x02, u16 w, u16 h, u16 d, RGB[w*h*d]]` (7-byte LE header — matches `renderPixelFrame` in `app.js`) and calls `WebSocketModule::broadcastBinary`. Skip-when-no-clients: a `hasClients()` pre-check avoids the memcpy when nobody is watching.
- [x] `modules/lights/ArtnetOutModule.h` — controls `source` (default `ripples-0`), `dest_ip` (default `255.255.255.255`), `universe` (0..15, default 0). In `loop20ms()`, reads `pixelBuffer()`, packs `ceil(count*3 / 510)` Art-Net OpDmx packets (18-byte header, 510 DMX bytes = 170 RGB per packet), universe increments per packet from the base, sends via `pal::Udp::send`. Packet header verified on the wire: `Art-Net\0` + `0x5000` OpDmx + ProtVer 14.
- [x] `src/pal/PalUdp.h` — `Udp::begin()` / `Udp::send(ip, port, data, len)`. ESP32 wraps `WiFiUDP` (`beginPacket`/`write`/`endPacket`), PC uses BSD `socket(SOCK_DGRAM)` + `sendto` with `SO_BROADCAST` enabled. 58 / 150 LOC.
- [x] `main.cpp` registers `ripples` / `preview` / `artnet-out`. Default boot still adds only the four head modules (`system / wifi-sta / http / ws`); lighting modules come in via the UI's "Add module" picker. Scheduler stays at 1 core on ESP32 — two-core lands in Sprint 7.
- [x] `src/modules/lights/` budget: 600 LOC in `check_loc.py` and `deploy.md`. Actual: 328 / 600 — comfortable headroom for Sprint 7's `FrameRing.h` + the PSRAM-allocation call sites.
- [x] `src/pal/PalUdp.h` budget: 150 LOC. Actual: 58 / 150.
- [x] HIL verification on `esp32dev` at `192.168.1.234`: open the device's frontend, add `ripples` + `preview` + `artnet-out` via the UI. Logs show `[ripples] allocated 16x16x1 = 768 bytes`. Art-Net listener on the host captures ~26 packets/sec at the 16×16×1 default — universe 0 carries 510 DMX bytes (170 RGB), universe 1 carries 258 DMX (86 RGB), total 768 channels = 16×16 RGB. Rate is below the 100 pps target because WiFi UDP latency stretches the single-core scheduler's 20 ms cadence; Sprint 7's core 1 will give Art-Net its own loop budget.
- [x] PC end-to-end verified separately: WS binary preview frame validates as `0x02 | w=16 | h=16 | d=1 | RGB[768]`, Art-Net listener on `127.0.0.1:6454` receives the same two-universe split.
- [x] Per-module footprint well under 300 LOC each: `RipplesEffect.h` 128 LOC, `PreviewModule.h` 106 LOC, `ArtnetOutModule.h` 107 LOC.

#### State persistence (LittleFS)

- [ ] `modules/system/StateStoreModule.h` — added FIRST in `main.cpp` after the four head modules. On `setup()`: reads `/modules.json` via `pal::fs_read_text`, for each `{type, id}` entry calls `manager_->add()` (skipping any id already present so the default head modules aren't duplicated). For each successfully-added module, reads `/state/<id>.json`, parses as JSON, calls `module->loadState(obj)` — restores per-control values onto the just-built module.
- [ ] `loop10s()` walks the current module list, builds the `modules.json` candidate JSON, compares to the last-written snapshot. If the list changed (add / remove / reorder), writes the new snapshot. Same cadence checks each module's `saveState` output vs its last-written snapshot; differences trigger a write of `/state/<id>.json`. State files for IDs that no longer exist are deleted.
- [ ] `WifiStaModule` already uses `pal::fs_write_text` for `/wifi.json` (Sprint 5) — that file stays orthogonal to the new `/modules.json` + `/state/*.json` scheme; WiFi creds live separately because they precede module construction.
- [ ] Frontend interactions through the existing endpoints — no protocol changes needed:
  - `POST /api/modules` (add) → next `loop10s` notices the new id → modules.json rewritten.
  - `DELETE /api/modules/<id>` (remove) → next `loop10s` notices the gone id → modules.json rewritten, `/state/<id>.json` deleted.
  - `POST /api/modules/reorder` → next `loop10s` notices the order change → modules.json rewritten.
  - `POST /api/control` / `PATCH /api/modules/<id>` → next `loop10s` notices the new state hash → `/state/<id>.json` rewritten.
- [ ] HIL: add `ripples` + `preview` + `artnet-out` via the UI, edit `speed` / `dest_ip`, wait ≥ 10 s, reboot the device (flash button or power-cycle). After WiFi reconnects, the three lighting modules are back at the same settings — confirmed via `/api/modules` and the live preview.
- [ ] `src/modules/system/` budget bump if needed for `StateStoreModule` — current `372 / 400`, expected ~100 LOC addition → budget 400 → 500 if it overflows. Bump signed off here.

### Pixel-buffer sharing — design note

Producer (`RipplesEffect`) owns `RGB pixels_[count]`. On every `onUpdate("count")` it reallocates and bumps `revision_++`. It implements the `PixelSource` interface from `Pixelable.h`:

```cpp
PixelBufferRef pixelBuffer() const override {
  return { pixels_, count_, revision_ };
}
```

**Why a registry, not `dynamic_cast`.** The natural shape — `dynamic_cast<PixelSource*>(manager_->find(id))` — does not work on hardware: arduino-esp32 builds with `-fno-rtti`. Adding `-frtti` is a hammer (binary size, framework-wide effect) and adding a virtual `asPixelSource()` to `MoonModule` violates the "nothing in core" rule for this sprint. The frugal answer: a small **publish-on-setup / find-by-id registry** living entirely in `modules/lights/`.

```cpp
// modules/lights/PixelRegistry.h  (≈ 30 LOC)
class PixelRegistry {
 public:
  static PixelRegistry& instance();
  void   publish(const char* id, PixelSource* s);
  void   unpublish(PixelSource* s);
  PixelSource* find(const char* id) const;
};
```

Producer side:

```cpp
void RipplesEffect::setup() {
  PixelRegistry::instance().publish(id().c_str(), this);
}
void RipplesEffect::teardown() {
  PixelRegistry::instance().unpublish(this);
}
```

Consumer side:

```cpp
void PreviewModule::setup() {
  source_ptr_ = PixelRegistry::instance().find(source_.c_str());
}
void PreviewModule::onUpdate(const char* k) {
  if (std::strcmp(k, "source") == 0)
    source_ptr_ = PixelRegistry::instance().find(source_.c_str());
}
void PreviewModule::loop20ms() {
  if (!source_ptr_) source_ptr_ = PixelRegistry::instance().find(source_.c_str()); // late-add tolerance
  if (!source_ptr_) return;
  PixelBufferRef ref = source_ptr_->pixelBuffer();
  if (ref.revision != last_rev_) { /* re-size derived state */ last_rev_ = ref.revision; }
  // hot path: read ref.data[0..ref.count] directly
}
```

**This is publish/subscribe — just the cheap version.** Publish happens on `setup()`, subscribe (= `find`) happens on `setup()` / `onUpdate("source")` / a fallback in `loop20ms` for tolerance to module-add order. The hot path is zero overhead: a cached pointer + one virtual call + one `uint32` revision compare. No event bus, no per-tick dispatch, no allocation. **Both consumers run on the same core as the producer in Sprint 6**, so no atomics are needed — a simple pointer read sees a consistent buffer because the scheduler ticks each module to completion before moving on. Sprint 7 splits one consumer off to core 1 and introduces an SPSC ring; the `PixelSource` interface and the registry stay unchanged.

| | Registry + polling (this sprint) | Full event bus (deferred) |
|---|---|---|
| Hot-path cost per tick | 1 virtual call + 1 u32 compare | event lookup + dispatch + queue ops |
| Allocation | none (post-setup) | per-event (or per-subscriber list mutation) |
| Code surface | ≈ 30 LOC interface + registry | ≈ 150 LOC bus + boilerplate |
| Best at | 1 producer + few consumers | many-to-many + selective updates |

### Deferred

- [ ] Two-core scheduler + SPSC ring + PSRAM buffers — promoted to Sprint 7 as the next focused sprint.
- [ ] Parent modules + child trees (`addChild`). Land when effect-on-effect composition arrives.
- [ ] Generic Producer / Consumer base classes. Sprint 7 ships one hand-rolled `FrameRing` in `modules/lights/`; promote to a generic interface only when a second producer/consumer pair appears.
- [ ] FastLED / WS2812 GPIO driver (and `PalGpio.h` + typed board-config codegen). Lands when there's a board with a strip wired up.
- [ ] Effect layering / blending. Comes with parent modules.
- [ ] Pub/sub event bus. Registry + (later, ring) is enough; revisit when many-to-many fan-out + selective updates demand it.

---

## Sprint 7 — Two-core + PSRAM scaling {#sprint-7}

> **Scope:** Scale the Sprint 6 light pipeline to 128×128 by moving `ArtnetOutModule` onto **core 1** with an SPSC ring across cores, allocating the effect's pixel buffers from **PSRAM** when available, and re-enabling multi-core scheduling on ESP32 (which Sprint 5 was forced to disable when arduino-esp32's std::thread overflowed its 3 KB pthread stack). Foundation modules from Sprint 6 (`RipplesEffect`, `PreviewModule`, `ArtnetOutModule`, the registry, the `PixelSource` interface) all carry through unchanged on their hot paths — only *how* `ArtnetOutModule` acquires its frame and *where* the effect allocates change.
>
> **Risks isolated by the Sprint 6 → 7 split:** atomic memory ordering on cross-core SPSC is easy to get wrong; PSRAM-backed buffers are slower than internal heap (~5–10 MB/s vs ~30 MB/s) and may need cadence tweaks; xTaskCreatePinnedToCore replacing std::thread needs stack-size validation. Each risk has a clean rollback (revert to Sprint 6) if it goes sideways.

### Definition of Done

- [ ] `src/pal/PalRtos.h` — `pal::task_create_pinned(fn, name, stack_bytes, arg, priority, core_id)`. ESP32: `xTaskCreatePinnedToCore` with the explicit stack size (avoids the pthread 3 KB default that bit Sprint 5). PC: `std::thread` (core_id ignored). Budget 80 LOC.
- [ ] `src/pal/PalHeap.h` — `pal::psram_alloc(bytes)` / `pal::psram_free(ptr)`. ESP32 with PSRAM: `heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM)`. ESP32 without PSRAM (classic `esp32dev` if not populated): falls back to internal heap via `heap_caps_malloc(MALLOC_CAP_INTERNAL)`. PC: regular `malloc` / `free`. Caller treats nullptr return as "alloc failed, skip frame" — no exceptions thrown. Budget 80 LOC.
- [ ] `modules/lights/FrameRing.h` — depth-2 SPSC ring carrying RGB buffers. Two slots allocated alongside the effect's working buffer (PSRAM-backed). `acquire_write_slot()` returns the slot the producer should fill; `publish()` advances the head atomic with release semantics. `try_acquire_read()` returns the most recent published slot or nullptr; `release_read()` advances the tail atomic. ≤ 80 LOC. Producer never waits — on full, overwrites (drop frame). At 50 fps with one consumer ~20 ms behind, both producer and consumer settle near depth 1.
- [ ] `MoonModule`: adds `uint8_t core_ = 0` field + `coreAffinity() const` accessor. `getSchema()` already emits a `core` field (Sprint 5 hardcoded it); this sprint wires it through. ~5 LOC change in core (the only one this release). Concrete modules set `core_ = 1` in their constructor when they want core 1; default stays 0.
- [ ] `Scheduler::core_loop` reads `m->coreAffinity()` instead of `i % cores` for module-to-core assignment. Each core ticks only its own modules. Sprint 5's "core 0 inline" pattern stays for the calling thread; the extra cores are spawned via `pal::task_create_pinned` at 8 KB stacks.
- [ ] `main.cpp` calls `pmm::Scheduler sched(&mm, 2)` on ESP32 (was 1 since Sprint 5).
- [ ] `RipplesEffect` switches to `pal::psram_alloc` for its working buffer + the FrameRing's two slots. Any dimension change reallocates all three and bumps `revision_`. `width` / `height` max bumped 64 → 128 (so 128×128×1 = 49 152 B per slot, ~144 KB total — comfortable in PSRAM).
- [ ] `ArtnetOutModule` switches data acquisition: instead of `source_->pixelBuffer()`, it grabs the source's `FrameRing*` (exposed via an extended `PixelSource::frameRing()` virtual that returns `nullptr` by default; `RipplesEffect` overrides it) and calls `try_acquire_read()` / `release_read()` around the packet-pack loop. `coreAffinity() = 1`.
- [ ] `src/modules/lights/` budget bumped 600 → 700 LOC for the FrameRing addition + `pal::psram_alloc` call sites.
- [ ] HIL verification on `esp32dev`: same scenarios as Sprint 6 still pass at 16×16×1 and 64×64×1. Confirm `[sched] core 1: entering loop` appears in `/api/log` (Sprint 5 never saw this).
- [ ] HIL verification on `esp32s3_n16r8`: scale `width` + `height` to 128 — `/api/system`'s `psram_used_kb` jumps by ~150 KB for the three slots, preview keeps rendering, Art-Net receiver still sees the stream (97 universes/frame at ~24 Mbps). On `esp32dev` (no PSRAM), bumping to 128 returns null from `psram_alloc`, effect logs `[ripples] alloc failed at 128x128`, downstream modules skip — no crash.
- [ ] Per-module footprint still ≤ 300 LOC each.

### Deferred

- [ ] Generic Producer / Consumer base classes. The Sprint 7 `FrameRing` is single-shape; promote when a second producer/consumer pair appears.
- [ ] Per-module core affinity via UI control. `core_` is hardcoded per module class; making it a settable schema control lands when there's user demand for runtime remapping.
- [ ] FastLED / WS2812 GPIO driver (still). Lands when a board with a strip is on the bench.

---

## To be validated during Release 1

The [process architecture](../architecture/process.md) states the contract in high-level terms. The items below are specifics inherited from v1's Release 9 guardrails outline that need to earn their place under the frugality rule (does this addition pay for itself?) before they are codified. Each is a sprint-time decision in Release 1, not a Release-1 entry condition.

**Tool choices for the three guardrail tiers.** Specific formatters, linters, and static analysers — for example `clang-format`, `ruff`, `clang-tidy` with `bugprone-*`/`modernize-*`, `cppcheck`. Each must justify itself or be dropped. The *rules* are fixed by process architecture; the *tools* are not.

**Hot-path enforcement mechanism.** Whether hot-path bans (no allocations, no blocking calls, no logging in `loop*()` bodies) are enforced by regex over `void <name>::loop\b.*\{` … matching `}`, by a clang-tidy plugin, by AST tooling, or by something else. Whatever it is must be cheap to maintain.

**Footprint baseline format.** Where module footprint baselines live (`baselines/footprint.json` was the v1 proposal) and what triggers an update — PR description entry, signed-off bump line, or another mechanism.

**Doc-growth budget number.** v1 proposed 500 lines per release; v2 picks a number when it has enough data to pick one.

**Structural-additions justification format.** Whether `// WHY:` (C++) and a top-of-file Python docstring are the right way to record why a new file in `scripts/`, `tests/`, or `docs/` exists, or whether a single mechanism (e.g. PR template field) is cheaper.

**Verifier-of-the-verifier.** v1's answer was: the testing system's growth is gated by the structural rule, and its correctness is asserted only by the unit test that every module's `healthReport()` is non-empty — no meta-tests. v2 inherits this stance by default but should validate it once `healthReport()` exists.

Each item closes with a one-line outcome in this section by Sprint 7 (kept, dropped, or replaced). What is kept moves into [process architecture](../architecture/process.md); what is dropped disappears.
