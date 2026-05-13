# Release 1 — Restart to Parity

> **Theme:** Bring v2 to parity with v1's first-boot pipeline — effect → blend → driver → preview, served over HTTP / WS with WiFi-STA, persisted to LittleFS, OTA-updatable, NTP-synced, ArtNet-capable — over six sprints. The decision to restart from v1 is documented in v1's [Release 9](https://ewowi.github.io/projectMM/development/release-09/). The contract under which this release executes is [process architecture](../architecture/process.md): minimalism, guardrails, anti-drift.

---

## Release Overview

### What Release 1 delivers

A v2 codebase that runs the same first-boot pipeline as v1, implemented as modules over a small core that fits in one head. The core is `Module` + `ModuleManager` + `Scheduler` + a minimal `Pal`; networking, OTA, NTP, persistence, the HTTP / WS server, and the entire lighting domain (`RGB`, `pixelBuf`, effects, layers, layouts, modifiers) are modules. The deploy pipeline is three scripts: build, flash, test. v2's first user is lights, but lights live in `modules/lights/` and depend on the core, not the other way around.

### Minimalism targets (CI-enforced)

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

### Mid-release pivot: port-and-minimize (2026-05-12)

Sprint 2 and Sprint 3 were initially attempted as greenfield rewrites of v1's HTTP server, WebSocket server, and frontend. The result was buggy distillations of code v1 had already debugged: TCP fragmentation, WebSocket handshake corner cases, threading races between the scheduler and HTTP handlers — all rediscovered, all fixed by reading v1's solutions after the fact. The first attempts were deleted; the sprints below are rewritten around *porting* v1's working code and minimizing it. The discipline is codified in [process architecture §4](../architecture/process.md#4-port-and-minimize-where-substantive-modules-come-from). Surviving artefacts from the first attempt: guardrails framework, `Module` / `ModuleManager` / `Scheduler` skeleton, `HelloModule`, factory, doctest harness, the test files covering the core and `HelloModule`.

---

## Sprints

| Sprint | Goal | Minimalism target |
|--------|------|------------------|
| [Sprint 1](#sprint-1) | Guardrails framework + empty Module/Manager/Scheduler/Pal skeleton + Linux PC CI green | core ≤ 300 LOC |
| [Sprint 2](#sprint-2) | Port `HttpServer` + v1 frontend bundle; UI shell visible at `:8080` (disconnected indicator OK) | network ≤ 500 LOC |
| [Sprint 3](#sprint-3) | `Module` → `MoonModule` (merge controls); port `WsServer` + frontend sources + `SystemStatusModule`; UI shows live system status | MoonModule ≤ 600, system ≤ 300 LOC |
| [Sprint 4](#sprint-4) | Pal-minimum + ESP32 builds green + on-target tests + HIL | Pal ≤ 200 LOC |
| [Sprint 5](#sprint-5) | WiFi-STA + REST + WebSocket over hardware; frontend connects unchanged | per-module ≤ 300 LOC |
| [Sprint 6](#sprint-6) | Light domain foundation + LittleFS state persistence: RipplesEffect + Preview + Art-Net out + modules.json / per-module state survive reboot | per-module ≤ 300 LOC |
| [Sprint 7](#sprint-7) | Two-core + PSRAM scaling: PalRtos pinned tasks, PalHeap PSRAM, FrameRing SPSC, ArtnetOut pinned to core 1, stress-test 128×128 on s3 | per-module ≤ 300 LOC |
| [Sprint 8](#sprint-8) | Test foundation: classified unit tests + behavioral coverage for Sprints 4–7, `[MemBoot]` / `[MemLive]` runtime events, declarative scenarios replayed in-process | `test/` grows to ~25 % of `src/` |
| [Sprint 9](#sprint-9) | Release 1 polish: per-file minimalism review (source + guardrails + `test/`), deploy walk via `scripts/moondeck.py`, docs read-through; tag `v1.0.0-foundation` | net LOC ≤ 0 across `src/` + `docs/` |

Each sprint closes with a working device (or working PC application for sprints 2–3). After Sprint 9, Release 1 is tagged `v1.0.0-foundation` — the v2 codebase has a real light pipeline, scaled persistence, a stress-tested dual-core path on hardware, a test surface that exercises what Sprints 4–7 actually built, and a reviewed, trimmed foundation that the next release inherits. The v1 → v2 cutover (rename + final stable tag) closes [Release 2](release-02.md), which adds ArtNet **in**, OTA, NTP, and any remaining v1 parity bits.

---

## Sprint 1 — Guardrails and skeleton {#sprint-1}

> **Scope:** Land the minimum guardrails framework that the empty `Module` / `ModuleManager` / `Scheduler` / `Pal` skeleton justifies — no more. The four lifecycle cadences (`loop`, `loop20ms`, `loop1s`, `loop10s`) are first-class scheduler concerns from commit 1, not afterthoughts. Linux-PC CI green; macOS/Windows PC and ESP32 envs land when those platforms gain real code (Sprint 3).

The framework is the load-bearing deliverable. Without it, the same drift that produced v1 reaches v2 by Sprint 4. [Process architecture](../architecture/process.md) is the spec; the first commit of this sprint is its implementation. `loop20ms()` is new in v2 (v1 had only `runLoop1s` via timing windows); the scheduler maintains four cadences per core (hot, 20 ms, 1 s, 10 s) and modules opt into each by overriding the corresponding method. Empty overrides cost nothing.

`scripts/moondeck.py` renders the project's process surface as a browser-based control panel from day one — see [Deploy → MoonDeck](../deploy.md#moondeck) and [process architecture §2](../architecture/process.md). Pre-commit and CI invoke scripts directly (non-interactive contexts).

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
- [x] Developer control panel: `scripts/moondeck.py` — see [Deploy → MoonDeck](../deploy.md#moondeck); per-script docs at [Deploy → Scripts](../deploy.md#scripts)
- [x] Release 5 evaluation sprint scheduled (see Release Overview above)

Deferred to Sprint 2+ as "earn its place":

- Footprint baseline, test-count baseline, doc-growth budget number — no data yet
- Drift-metrics status doc — nothing to measure on an empty skeleton
- Formatters / linters / static analysers (clang-format, ruff, clang-tidy, cppcheck) — see "To be validated during Release 1" below

---

## Sprint 2 — Port `HttpServer` + serve v1 UI shell {#sprint-2}

> **Pivot:** A first pass at this sprint built `HttpServerModule` from scratch and ran into the classes of bugs (TCP fragmentation, threading races, HTTP body parsing edge cases) that v1's `core/HttpServer.h` has already debugged into stability. The sprint was reset: substantive modules in v2 are *ported* from v1, then minimized. See [process architecture §4](../architecture/process.md#4-port-and-minimize-where-substantive-modules-come-from). Survivors of the first attempt: guardrails framework, core skeleton, `HelloModule`, `ModuleManager` factory, doctest harness.

> **Scope:** Port v1's `src/core/HttpServer.h` (332 LOC) — landing at `src/pal/PalHttp.h` because it is a platform-conditional header wrapping `cpp-httplib`/`ESPAsyncWebServer` — and v1's `src/frontend/frontend_bundle.h` (the gzipped SPA). Minimize `PalHttp.h`. Wrap it as a platform-neutral `HttpServerModule` (no `#ifdef`s) that serves `/` from the bundle and exposes minimal REST so v1's UI shell loads and renders. The bundle's WebSocket reconnect logic shows a "disconnected" indicator until Sprint 3 lands the WS module — that is the expected end-of-Sprint-2 visual state. PC only — ESP32 envs land in Sprint 4 but `PalHttp.h`'s `#ifdef ARDUINO` branch is kept intact so it compiles when ESP32 arrives.

### Definition of Done

- [x] `HelloModule` — `Module` subclass with counter (`loop1s`) and enabled flag; `serialize_json` override
- [x] Module factory in `ModuleManager`: `register_type` + `std::function` registry; `add(type, id)` dispatches through it (no `strcmp` chain); `find(id)` added
- [x] Test-count baseline dropped — counting test *files* adds friction without catching real drift; LOC budgets and the structural allowlist are sufficient
- [x] Vendor cpp-httplib v0.18.5 in `lib/httplib/src/` (header-only) — see [ADR 0001](../adr/0001-vendor-cpp-httplib.md); `lib` added to structural allowlist
- [x] Port v1's `HttpServer.h` verbatim into `src/pal/PalHttp.h` (port-step, no modifications) — lives in `pal/` because it contains the only platform conditional in v2; module code that uses HTTP gets the abstraction, not the conditional
- [x] New guardrail `scripts/check_platform_guards.py` rejects `#ifdef ARDUINO` / `#include <Arduino.h>` / ESP-IDF headers outside `src/pal/`; wired into pre-commit + CI + moondeck.py
- [x] **Minimize** `PalHttp.h` under the §4 rule (strip patches, not features): read line by line, looking for retries / swallow-everything try/catch / timer band-aids / re-init paths. Result: **no patches over symptoms found**; LOC unchanged from v1 verbatim (332). Examined and kept:
  - 4× `catch (std::bad_alloc&)` blocks in ESP32 handlers (return 503 instead of crash) — graceful API boundary for ESP32's tight heap; a deliberate architecture decision, not a patch.
  - `catch (...)` around `listen_after_bind()` in PC branch — broader than ideal, but logs to stderr rather than swallowing silently. Acceptable.
  - Trailing `/.+` → `/*` wildcard adaptation in `onDelete`/`onPatch` — cross-platform syntax adapter between cpp-httplib regex and ESPAsyncWebServer glob; deliberate.
  - `std::map<request, body>` buffering for ESP32 `onPost`/`onPatch` — required by ESPAsyncWebServer's chunked-body API; not a patch.
  - `onPostBinary` + `BinaryChunkFn`/`BinaryEndFn` — kept for Sprint 7 (`FirmwareUpdateModule` OTA upload). Future-needed feature; per §4, not a subtraction target.
- [x] `HttpServerModule` (in `src/modules/network/`) wraps `pal::HttpServer`, registers `GET /api/modules` (list — matching v1's route set; v1 has no GET-by-id and the WebSocket schema covers per-module reads). Mutations (`POST` / `DELETE` / `PATCH`) land in Sprint 3 with the control system. Contains **zero** platform conditionals.
- [x] Port v1's `src/frontend/frontend_bundle.h` verbatim into `src/frontend/frontend_bundle.h` (generated data — not counted by LOC checks)
- [x] `HttpServerModule` serves the bundle on `GET /` via `pal::HttpServer::onGetStaticGzip` with `Content-Encoding: gzip`
- [x] **End-of-sprint verification:** open `http://127.0.0.1:8080`, see v1's UI render, see modules list (just `hello-0`), see the WebSocket-disconnected indicator (because Sprint 3 hasn't landed yet)
- [x] LOC budgets in range: `src/pal/PalHttp.h` 247 / 350 (v1 verbatim 332; the port-and-minimize step found no patches), `src/modules/network/` 41 / 250
- [x] Host unit + integration tests via doctest: three `HttpServerModule` cases covering `GET /`, `GET /api/modules`, and unknown-route 404. Probe uses a 30-line raw-TCP helper rather than `httplib::Client` — cpp-httplib's Client returns `"Failed to read connection"` when used in the same process as its Server on macOS (production code is Server-only and works fine; reason captured in `test_http.cpp`).

---

## Sprint 3 — `MoonModule` + `WsServer` + `SystemStatusModule` + frontend sources {#sprint-3}

> **Scope:** Three substantive ports land together because they validate each other: the control system (lifecycle in v2's `Module` + control mechanism from v1's `StatefulModule`, merged into a single `MoonModule` class), the WebSocket transport (`PalWs.h` + `WebSocketModule`), and a real module that exercises both (`SystemStatusModule`). The v1 frontend *sources* (HTML/CSS/JS + bundle generator) come in too, minimized to render only what v2 ships. After this sprint the UI shows live system status, controls are interactive, and the bundle can be regenerated from sources in-tree.

### Definition of Done

#### Step 1: `MoonModule` (merge `Module` + v1's `StatefulModule`)

- [x] Rename v2's `src/core/Module.h` → `src/core/MoonModule.h`; `pmm::Module` → `pmm::MoonModule` across the tree (HelloModule, HttpServerModule, ModuleManager, Scheduler, tests). `ModuleManager` keeps its name (it manages `MoonModule` instances; the asymmetry is honest).
- [x] Add ArduinoJson via `lib_deps` (ADR 0002 codifies registry-vs-vendor rule)
- [x] Merge: v1's control system (`addControl` × 10, `setControl`, `getSchema`, `getControlValues`, `clearControls`, pending props, state persistence, children, autowire hooks, `fillSystemJson`) merged into `MoonModule`. v2's tiered cadences kept (`loop20ms`/`loop1s`/`loop10s` — v1 has fewer). CRTP `StatefulModule<Derived>` wrapper dropped (single class).
- [x] **Minimize** the merger under §4 — five refinements landed beyond a 1:1 port:
  1. Factory-injected `classSize` via `ModuleManager::register_type<T, Args...>()` — captures `sizeof(T)` once, modules write zero per-class boilerplate (replaces v1's CRTP).
  2. `onAllocateMemory()` generalizes v1's `onSizeChanged` (was lighting-only) to a no-args reallocation hook.
  3. `onBuildControls()` replaces v1's `rebuildControls()` duplication — modules put all `addControl()` calls here, framework calls it from `runSetup()` and external code calls it directly for rebuilds.
  4. `dynamicMemorySize()` derived from a single cached value the module sets in its `onAllocateMemory()`. No separate `heapSize()` to drift; PSRAM/heap not split (when PSRAM is present it's used by default; reporters care about the total).
  5. Single-method public API per concern — no `rebuild*()` wrappers; the `on*()` methods are the sole entry points. Modules pay one line of bookkeeping in the override (set `moduleAllocBytes_`; call `clearControls()` if supporting rebuild) instead of two methods per concern in the API.
- [x] **Split** into `src/core/MoonModule.h` (declarations + small inlines, 122 LOC) + `src/core/MoonModule.cpp` (substantive bodies, 328 LOC). Consistent with v2's existing pattern (ModuleManager and Scheduler are also split). Total 450 LOC vs v1's 875 inline (51%).
- [x] LOC budgets: `src/core/MoonModule.h` ≤ 250 (122 actual), `src/core/MoonModule.cpp` ≤ 350 (328 actual). `src/core/` ≤ 300 for ModuleManager + Scheduler only (excludes nested MoonModule.{h,cpp}).

#### Step 2: WebSocket transport (`PalWs.h` + `WebSocketModule`)

- [x] Port v1's `src/core/WsServer.h` into `src/pal/PalWs.h` (483 LOC → 247, minimized). ESP32 branch: deferred frame-buffer pre-allocation infrastructure + heap_caps_get_largest_free_block guards (Sprint 4, when PalHeap lands); deferred broadcastLog (Sprint 5+, when v2 has logging). PC branch: inlined the POSIX socket calls instead of carrying v1's `PcSocketShims.h` (Windows path dropped — v2 targets Linux/macOS PC + ESP32).
- [x] Wrap as `WebSocketModule` in `src/modules/network/` — platform-neutral, no `#ifdef`s; owns a `pal::WsServer`, broadcasts schema + state JSON each `loop1s` when there are connected clients. **Deviation from initial plan**: WS lives on its own port (81), not as a `/ws` upgrade on the HTTP port. cpp-httplib has no WebSocket support and adding HTTP-upgrade handling would force a second HTTP library — v1's two-port pattern is cleaner here. Frontend connects to `ws://host:81/`.
- [x] REST mutations on `HttpServerModule`: `POST /api/modules` (add by type+id), `DELETE /api/modules/{id}` (remove), `PATCH /api/modules/{id}` (set controls — body is a JSON object of `{key: value, ...}`, each key dispatched through `setControl`). All hold `manager_->mutex()` across find+mutate to avoid races with the Scheduler.
- [x] **Bug uncovered + fixed during Step 2**: `ModuleManager::add()` was calling `m->setup()` directly instead of `m->runSetup()`. This skipped the framework's auto-registration of the `enabled_` control and any other onBuildControls work — meaning `setControl("enabled", false)` silently returned false because the control wasn't registered. Both `add()` and `remove()` now use `runSetup()` / `runTeardown()` dispatch wrappers; `Scheduler::core_loop` correspondingly uses `runLoop` / `runLoop20ms` / etc. so child recursion + enabled-gating in the dispatch wrappers actually fire.
- [x] **End-of-step verification**: HTTP REST exercised by curl — POST adds, PATCH `{"enabled":false}` stops `HelloModule::counter_` from incrementing (visible in subsequent GET), DELETE removes; raw WS probe sees the 101 handshake and receives schema + state frames at ~2/sec (matching `loop1s`).
- [x] LOC: `src/pal/PalWs.h` 247 / 450 (v1 verbatim was 483; ~51% size); `src/modules/network` 156 / 250 (HttpServerModule + WebSocketModule).

#### Step 3: `SystemStatusModule` (the first real `MoonModule`)

- [x] `src/pal/PalSystemInfo.h` ports v1's system-info `pal::*` accessors as a v2 pal-domain file. **PC stubs** for now — `chip_model="pc"`, `mac_address=""`, `total_heap_kb=0`, `cpu_cores=hardware_concurrency`, `local_time_str` via `localtime_r+strftime`, etc. Real ESP32 implementations land in Sprint 4 under `#ifdef ARDUINO` HERE in pal/, never in the modules. 48 / 200 LOC.
- [x] `SystemStatusModule` ported from v1's 198-LOC `SystemStatus.h`. Inherits `MoonModule` (not v1's `StatefulModule<Derived>` — Step 1c's factory-injected `classSize` replaced the CRTP layer). All 28 `addControl(...)` calls moved from `setup()` into `onBuildControls()` per Step 1c refinement #3 — `setup()` does the one-time hardware reads (`chip_model`, totals); `loop1s()` samples dynamic fields (heap, temp, time, fps). **Zero platform conditionals** in the module — enforced by `check_platform_guards.py`. 147 / 300 LOC.
- [x] `main.cpp` swaps `mm.add("hello", ...)` → `mm.add("system", "system-0")`; `src/modules/hello/HelloModule.h` and `test/test_pc/test_hello.cpp` deleted (mandatory subtraction for Sprint 3 close). `test_http.cpp` + `test_module.cpp` updated to use `SystemStatusModule` as the test fixture.
- [x] **Wire-format alignment with v1's frontend** (uncovered when the browser saw no controls): v2's `WebSocketModule` was emitting `{"event":"schema",...}` + a wrapped state envelope `{"event":"state","modules":[...]}` with flat key:value entries — but v1's `app.js` dispatches on `msg.t === 'schema'` and expects state as a **raw top-level array** `[{id, controls:{...}}, ...]`. The reinvention had no justification. v2 now emits v1's exact wire format: schema is `{"t":"schema","modules":[...]}`, state is a raw array with each entry `{id, controls:{key:value, ...}}`. Port-and-minimize default applied — use v1's working design, don't reinvent.
- [x] **End-of-step verification**: open WebSocket on `:81`, see `system-0` schema with 28 controls (uptime_s, fps, local_time, heap_*, psram_*, fs_*, chip_model, mac_address, firmware_version, build_date, build_time, cpu_*, flash_*, reset_reason). State frames show `uptime_s` and `local_time` advancing each second. Browser hits `http://127.0.0.1:8080`, sees v1 UI render system status live with all 28 controls.

#### Step 4: Frontend sources

- [x] Ported v1's `src/frontend/index.html` (84 LOC), `style.css` (744 LOC), `app.js` (1647 LOC) verbatim — byte-identical to v1.
- [x] Ported `scripts/gen_frontend_bundle.py` (v1's PIO pre-script handling dropped — v2 generates on-demand, not as part of the PlatformIO build). Generator made **deterministic** by setting `gzip mtime=0`; v1's version embedded the current timestamp, so two regenerations produced different bytes — that prevented any meaningful drift check. v2's drift check now works because identical sources always produce identical bundles.
- [x] `scripts/check_bundle.py` regenerates the bundle in-memory and diffs against the committed `frontend_bundle.h`. Wired into pre-commit + CI + moondeck.py. Drift between sources and bundle fails CI.
- [x] `scripts/moondeck.py` gains two cards: "Frontend bundle drift" (runs check_bundle) and "Regenerate frontend bundle" (runs gen).
- [x] **Minimize the frontend** under §4: the deliberation yields **nothing stripped**. The §4 rule is "future-needed features stay" — and the v1 frontend's lighting UI is needed by Sprint 6, WiFi UI by Sprint 5, firmware-upload UI by Sprint 7. All three sprints land in this release. Stripping any of it now would only mean re-porting later — exactly the waste §4 exists to prevent. The earlier Sprint 3 DoD bullet that proposed stripping "UI for features v2 does not yet ship" contradicted §4 and was wrong; the frontend is left as v1 ships it. Real UI changes happen *when* those sprints add their own features and need their own frontend bits.
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
- [x] `scripts/moondeck.py` gains a USB-port picker in the header (auto-populates from `/dev/cu.usb*` / `/dev/ttyUSB*` + `/dev/ttyACM*`, persisted via `localStorage`) and six ESP32 cards: Build esp32dev / esp32s3_n16r8, Flash esp32dev / esp32s3_n16r8 (consume the picker), Serial monitor for each env (long-running, consume the picker). `scripts/flash.py` and `scripts/monitor.py` are the underlying CLIs; CI doesn't use them (no hardware). `scripts/_pio.py` resolves the right `pio` binary — prefers `~/.platformio/penv/bin/pio` (PlatformIO's bundled Python 3.11) over a Homebrew shim that may resolve to a Python 3.12 with a system `fatfs` package whose API doesn't match the espressif32 platform's expectations (causes `ImportError: cannot import name 'create_extended_partition' from 'fatfs'` at build start). Falls back to PATH lookup when the penv isn't present (CI containers install PlatformIO via pip).

### Deferred (minimalism)

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
> Minimalism first: **no parent modules**, **no producer / consumer base classes**, **no SPSC ring**, **no PSRAM allocator**, **no per-module core affinity**, **no effect layering**, **no FastLED / WS2812 driver**. The data flow lives in three lighting modules + four headers in `modules/lights/`, one new pal file, one new state-store module. `src/core/` is untouched — the StateStoreModule walks `manager_` from the outside and uses the existing `MoonModule::saveState` / `loadState` / `getControlValues` API.

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

**Why a registry, not `dynamic_cast`.** The natural shape — `dynamic_cast<PixelSource*>(manager_->find(id))` — does not work on hardware: arduino-esp32 builds with `-fno-rtti`. Adding `-frtti` is a hammer (binary size, framework-wide effect) and adding a virtual `asPixelSource()` to `MoonModule` violates the "nothing in core" rule for this sprint. The minimal answer: a small **publish-on-setup / find-by-id registry** living entirely in `modules/lights/`.

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

- [x] `src/pal/PalRtos.h` — `pal::task_create_pinned(fn, name, stack_bytes, arg, priority, core_id)`. ESP32: `xTaskCreatePinnedToCore` with the explicit stack size (avoids the pthread 3 KB default that bit Sprint 5). PC: `std::thread` (core_id ignored). Actual: 35 / 100 LOC.
- [x] `src/pal/PalHeap.h` — `pal::psram_alloc(bytes)` / `pal::psram_free(ptr)`. ESP32 with PSRAM: `heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`. ESP32 without PSRAM: falls back to DRAM via `heap_caps_malloc(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)`. PC: regular `malloc` / `free`. Caller treats nullptr return as "alloc failed, skip frame" — no exceptions thrown. Actual: 24 / 100 LOC. **Incident:** the first cut omitted `MALLOC_CAP_8BIT`; on classic esp32 the `MALLOC_CAP_INTERNAL` fallback could hand back an IRAM-region buffer that only supports 32-bit-aligned word access. The first byte-store from the effect (`pixels[i].r = ...`) crashed the CPU with `LoadStoreError`. Pinning `MALLOC_CAP_8BIT` on both caps restricts the choice to DRAM (and to byte-addressable PSRAM on the s3), eliminating the fault class.
- [x] `modules/lights/FrameRing.h` — depth-2 SPSC ring carrying RGB buffers. Two slots allocated alongside the effect's working buffer (PSRAM-backed). `acquire_write_slot()` returns the slot the producer should fill; `publish()` advances the head atomic with release semantics. `try_acquire_read()` returns the most recent published slot or nullptr; `release_read()` advances the tail atomic. Producer never waits — on full, overwrites (drop frame). At 50 fps with one consumer ~20 ms behind, both producer and consumer settle near depth 1. Actual: 99 LOC (over the predicted 80 — the extra ~20 lines are the memory-ordering header comment, which earned its keep when the consumer moved cross-core).
- [x] `MoonModule`: adds `uint8_t core_ = 0` field + `coreAffinity() const` accessor + `setCoreAffinity(uint8_t)`. `getSchema()` already emits a `core` field (Sprint 5 hardcoded it); this sprint wires it through. Concrete modules set `core_ = 1` in their constructor when they want core 1; default stays 0. `ArtnetOutModule` is the only Release-1 module that opts in.
- [x] `Scheduler::core_loop` reads `m->coreAffinity()` instead of `i % cores` for module-to-core assignment. Each core ticks only its own modules. Sprint 5's "core 0 inline" pattern stays for the calling thread; the extra cores are spawned via `pal::task_create_pinned` at 8 KB stacks.
- [x] `main.cpp` calls `pmm::Scheduler sched(&mm, pal::default_scheduler_cores())`. `default_scheduler_cores()` returns 2 on ESP32 (was 1 since Sprint 5) and 2 on PC.
- [x] `RipplesEffect` switches to `pal::psram_alloc` for its working buffer + the FrameRing's two slots. Any dimension change reallocates all three and bumps `revision_`. `width` / `height` max bumped 64 → 128 (128×128×1 = 49 152 B per slot, ~144 KB pixel + ring; comfortable in PSRAM). **Perf tune:** the per-pixel `sqrt + cos + HSV→RGB` hot loop fell over at 128×128 (~4 fps). Replaced with two precomputed w·h tables — `phase_offset_` (Q16, uint16) and `base_color_` (full-bright HSV→RGB) — plus a function-static 256-entry cos-brightness LUT. Inner loop is now one Q16 subtract, one LUT load, three uint8 mul-shifts per pixel. `system_fps` at 128×128×1 went 4.5 → ~1700 (≈ 375×). Adds ~80 KB of PSRAM tables at 128×128 (16 384 × (2 + 3) bytes); rebuilt on geometry change, and just the color table on `hue_base` change.
- [x] `ArtnetOutModule` switches data acquisition: instead of `source_->pixelBuffer()`, it grabs the source's `FrameRing*` (exposed via an extended `PixelSource::frameRing()` virtual that returns `nullptr` by default; `RipplesEffect` overrides it) and calls `try_acquire_read()` / `release_read()` around the packet-pack loop. `setCoreAffinity(1)` in the constructor.
- [x] `src/modules/lights/` budget stays at 600. The Sprint 7 plan predicted a 600 → 700 bump for FrameRing + PSRAM call sites; actual is **489 / 600** even after FrameRing.h (99 LOC) and the LUT tuning. The bump didn't pay for itself, so it didn't land — bullet closed as "predicted growth did not materialise".
- [x] HIL verification on `esp32dev` (`/dev/cu.usbserial-20213431`, `192.168.1.234`, no PSRAM): persisted Sprint 6 modules restored (ripples-0 / preview-0 / artnet-out-0). 16×16 baseline: heap free 172 KB, no crash. Scaled to 64×64×1: heap free dropped to 105 KB (~67 KB consumed for pixels + tables + ring), no crash. Pushed to 128×128×1: log shows `[ripples] alloc failed at 128x128x1 (49152 + 32768 + 49152 bytes)`, effect leaves `pixels_ = nullptr`, downstream modules skip, heap recovers to 174 KB, `is_crash: false`, system continues. Bonus: at 128×64×1 the pixel buffer + tables fit but the ring slots don't — logged `ring alloc failed at 128x64x1 — Art-Net out won't have a feed` and downstream still skips cleanly. `coreAffinity` confirmed: artnet-out-0 reports `core=1` in `/api/modules` and ticks independently of the rest.
- [x] HIL verification on `esp32s3_n16r8` (`/dev/cu.usbmodem2021401`, `192.168.1.156`, 8 MB PSRAM): scaled to 128×128×1, log shows `allocated 128x128x1 = 49152 bytes (+ tables 81920 B + ring 2x49152 B)`. PSRAM drop ~225 KB total (pixels + tables + ring + preview frame); 7800+ KB still free. Preview WebSocket streams the binary frame; Art-Net receiver at `192.168.1.70:6454` visually confirmed receiving the stream (97 universes per frame, packed via the cross-core SPSC ring on core 1 with no per-frame copies in the consumer hot path).
- [x] Per-module footprint still ≤ 300 LOC each. Largest is `RipplesEffect.h` at 238 / 300 (grew from 128 in Sprint 6, mostly the two precompute helpers and the LUT initializer).

### Deferred

- [ ] Generic Producer / Consumer base classes. The Sprint 7 `FrameRing` is single-shape; promote when a second producer/consumer pair appears.
- [ ] Per-module core affinity via UI control. `core_` is hardcoded per module class; making it a settable schema control lands when there's user demand for runtime remapping.
- [ ] FastLED / WS2812 GPIO driver (still). Lands when a board with a strip is on the bench.

---

## Sprint 8 — Test foundation: classified units + MemBoot/MemLive + in-process scenarios {#sprint-8}

> **Scope:** The pre-Sprint-8 test surface (203 LOC / 9 cases under `test/test_pc/`) was honest smoke coverage but did not exercise what Sprints 4–7 built. This sprint landed three rails that give Sprint 9 a real safety net for the minimalism review and give every future sprint a place to assert behavior:
>
> 1. **Classified unit tests + behavioral coverage** of the Sprint 4–7 surface (FrameRing SPSC, RipplesEffect LUT, PalHeap fallback, Scheduler core affinity, PreviewModule wire format, ArtnetOutModule packet packing, StateStoreModule round-trip).
> 2. **Runtime `[MemBoot]` / `[MemLive]` events** emitted through `pmm::Logger` so heap deltas are visible in stdout / `/api/log` / the moondeck.py log window.
> 3. **Declarative scenarios replayed in-process** via doctest — `test/test_pc/scenarios/*.json` parsed and run through `ModuleManager`, with Rail 2's events firing during replay.
>
> **Minimalism stance, fixed by Sprint 1's Rule #1.** Events flow to `pmm::Logger`'s ring → serial / `/api/log` / moondeck.py log window. The firmware writes no log file; no `.md` status doc is generated; no per-chip baseline JSON is committed. Yesterday's run is not preserved on disk. Each deferred test artefact (REST scenario runner, baseline diffing, devicelist orchestration, summarise.py, committed serial logs) has an explicit drift episode that unlocks it — see [Deferred](#sprint-8-deferred) below. v1 reached 20+ test-artefact files; v2 stays at zero until drift demands the first one.

### Definition of Done

#### Rail 1 — Classified unit tests + behavioral coverage

- [x] `scripts/classify_tests.py` (51 LOC): regex over PIO's test output lines (`<file>:<line>: <name>\t[STATUS]`) emits a `[smoke]` / `[format]` / `[behavioral]` / `[integration]` prefix per case. Classifier keys ported from v1's `deploy/unittest.py`. Sprint 8 close distribution: 24 behavioral, 4 integration, 2 format, 1 smoke.
- [x] Seven new `test/test_pc/` files covering real call sequences from Sprints 4–7:

    | File | What it asserts |
    |---|---|
    | `test_ripples_lut.cpp` | `revision_` bumps on geometry change; `loop20ms` produces non-trivial pixel output; `hue_base` change recolours >90 % of the buffer (proves `rebuild_color_table_` runs without the LUT phase table being touched); registry publish/find round-trips. |
    | `test_frame_ring.cpp` | `allocate(0)` no-op; nullptr before first publish; publish→read returns the just-filled slot; slot rotation (depth-2 alternation); SPSC consumer never tears under a paced producer (10 kHz). |
    | `test_pal_heap.cpp` | `psram_alloc(0)` returns null; `psram_free(nullptr)` is a no-op; allocator returns byte-addressable memory; 1000 alloc/free cycles do not leak. |
    | `test_scheduler_affinity.cpp` | Two-core run ticks both modules; one-core run ignores the module pinned to core 1 (load-bearing: dispatch filter is `affinity == core_id`, not `affinity < cores`). |
    | `test_preview_wire.cpp` | 4×4 RGB grid packs to exactly the 7-byte header + body bytes the frontend's `renderPixelFrame` expects; 128×128 geometry encodes 0x80 0x00 LE in bytes 1–2. |
    | `test_artnet_packing.cpp` | OpDmx header bytes 0..17 match the Art-Net spec for universe 0 / 510 DMX; universe 96 fits in the low byte; partial DMX byte count encodes big-endian; 64×64 frame splits into 25 universes (24×510 + 1×48). |
    | `test_state_store.cpp` | Add `ripples` + `state-store`, scale width/height to 64, trigger `runLoop10s`, tear the manager down, instantiate a fresh manager → `state-store`'s setup re-creates `r-roundtrip` at 64×64 from disk. |

- [x] Two small production refactors enable testing without spinning up real WS / UDP: `PreviewModule::pack_frame` + `required_frame_bytes` and `ArtnetOutModule::pack_header` are now public static helpers. `loop20ms()` behavior unchanged on both modules.
- [x] `test/test_pc/` grew from **203 → 597** non-blank-non-comment LOC, 9 → 34 cases. Per-file LOC budgets added to `scripts/check_loc.py` with the post-write count + ~15 % headroom.
- [x] **Bugs the tests caught while being written** (the load-bearing payoff):
  - First `test_frame_ring` SPSC test assumed "no tears under any rate" — actually documented as best-effort overwrite. Test now pinned to the *paced-producer* contract that holds in production.
  - First `test_ripples_lut` test assumed `hue_base=0` ⇒ red-dominant. Wrong at 16×16 because hue rotates +0.05 per unit distance, reaching cyan at corners. Replaced with a recolour-on-change assertion.
  - `test_scheduler_affinity` surfaced a use-after-free between Scheduler instances: `pal::task_create_pinned` detaches std::threads on PC, so a destroyed `Scheduler`'s detached thread keeps reading `stop_` from the deleted instance. Test helper now sleeps 50 ms after `stop()`. Worth recording for Sprint 9's review of the Scheduler PC path; production unaffected because `run()` never returns on device.

#### Rail 2 — `[MemBoot]` / `[MemLive]` runtime events

- [x] `src/modules/system/MemTracker.h` (75 LOC): `snapshot()`, `log_boot()`, `log_live()`, `tick_1s()`. All events flow through `pmm::Logger`'s ring → serial + `/api/log` + moondeck.py log window. **No file written by firmware. No `docs/status/*.md` generated.**
- [x] `MoonModule::runSetup` hooked: snapshot heap before `setup()`, emit `[MemBoot]` after `setup()` returns (setup-only cost), emit `[MemLive]` at end of `runSetup` (total cost including children + `onAllocateMemory`). Originally planned as "one second later"; landed as "end of `runSetup`" — same shape as v1, and the MemBoot→MemLive delta on PR (PSRAM) cleanly exposes `onAllocateMemory`'s per-module cost separately from `setup()`. Lazy post-setup allocations (e.g. WiFi internal buffers) surface in the periodic delta emission instead.
- [x] `SystemStatusModule::loop1s` calls `memtracker::tick_1s()`. Emits `[MemLive] delta ±N.NKB = M.MKB (...)` lines only when heap moves > 1 KB; `[MemLive] periodic = M.MKB (...)` heartbeat every 10 s. Threshold + heartbeat cadence keep the log quiet when nothing happens, matching the "events only" stance.
- [x] HIL verified on esp32s3 (`192.168.1.156`): per-module setup brackets visible with real heap + PSRAM deltas, e.g. `ripples-0` MemBoot→MemLive on PR exposes a 3.6 KB PSRAM consumption attributable to `onAllocateMemory`'s pixel buffer + tables + ring. `/api/log` carries the live delta/periodic events.
- [x] `src/modules/system` LOC budget bumped 500 → 600 (MemTracker.h ~75 LOC; signed off).
- [x] **Layering note** (for Sprint 9): `src/core/MoonModule.cpp` now includes `src/modules/system/MemTracker.h`, which transitively includes `Logger.h`. This is the first `src/core/` → `src/modules/system/` dependency. Acceptable because Logger is a project-wide utility, not a feature module (CLAUDE.md's "in core" carve-out scopes the rule to networking / lighting). Worth re-evaluating in Sprint 9 if the convention bends further.
- [x] **Known limitation** (deferred): PC `PalSystemInfo` stubs return 0, so PC events show all-zero deltas. Real PC heap accounting via `mach_task_self`/`task_info` (macOS) and `/proc/self/status` (Linux) lands when a drift episode unlocks it — e.g. when Rail 3's scenarios need PC numbers for regression bounds.

#### Rail 3 — Declarative scenarios, in-process only

- [x] `test/test_pc/scenarios/base-pipeline.json` describes the current default light pipeline: add `ripples-0` + `preview-0` + `artnet-out-0` at 16×16, then scale to 32×32, then change `hue_base`. Step ops: `add_module`, `set_control`. The artnet-add step carries a `bounds.module_count.min = 3` assertion. Schema mirrors v1's scenario JSON so a REST runner can be unlocked later without a fixture rewrite. Co-located under `test/test_pc/scenarios/` (not v1's `deploy/test/scenarios/`) because scenarios in v2 are test fixtures, not deploy artefacts, and v2 deliberately has no `deploy/` directory.
- [x] `test/test_pc/test_scenarios.cpp` (91 LOC): parses each JSON under `test/test_pc/scenarios/`, replays via `ModuleManager`, asserts step bounds. `SUBCASE` per scenario so a failure in one doesn't mask others. Rail 2's `[MemBoot]` / `[MemLive]` events fire during replay — the test stdout shows the same memory trail a real boot would emit.
- [x] No REST runner. No `scripts/scenario.py`. No baseline file. Adding a new scenario requires no new test code — the directory glob picks it up automatically.

#### Growth ledger

- [x] [Artefact promotions](#artefact-promotions) subsection added under [Validated during Release 1](#validated-during-release-1). Empty at Sprint 8 close by design. Each future promotion of a deferred test artefact lands as one dated line citing the drift episode that unlocked it. Without a drift episode, no promotion. The [§3 anti-drift rule](../architecture/process.md#3-anti-drift-why-these-rules-survive) applied to test infrastructure itself.

### Deferred {#sprint-8-deferred}

Each deferred artefact lists the drift episode that would unlock its promotion:

- [x] ~~**REST scenario runner** (`scripts/scenario.py` against a live device)~~ — **PROMOTED 2026-05-13.** See [Artefact promotions](#artefact-promotions).
- [ ] **Per-chip baseline JSON** (`deploy/test/scenario-baseline.json`) — unlocks when a slow numeric regression slips past because today's number looked normal relative to last week. Introduce baseline diff for the one metric that drifted, not all metrics.
- [ ] **Devicelist + parallel orchestration** — unlocks when more than one device is tested *every PR*. Today: one s3 at 192.168.1.156, optionally an esp32dev at 192.168.1.234.
- [ ] **Committed `deploy/run/*.log` serial-log artefacts** — unlocks when a diff episode requires last week's serial output to spot today's drift. Default: read the log live in moondeck.py; no commit.
- [ ] **Status-doc aggregator** (`summarise.py` → `docs/status/index.md`) — unlocks when more than one human reads test results regularly. Today: one human.
- [ ] **`test_techdebt.cpp`-style encoded TODO tests** — do not promote. v1 drift candidate. Use ADRs with explicit closure dates instead.
- [ ] **`test_health_checks.cpp` as the "verifier-of-the-verifier"** — already DROPPED in [Validated during Release 1](#validated-during-release-1).

### Tools investigation — orchestration alternatives to `scripts/moondeck.py` {#sprint-8-tools-investigation}

Post-Sprint-8 evaluation triggered by "are there alternatives to moondeck.py?". Recorded here so Sprint 9's deploy walk doesn't re-litigate the same options, and so the rejection rationale is preserved against future re-proposals.

`scripts/moondeck.py`'s load-bearing role is the [§2 process-visibility rule](../architecture/process.md#2-guardrails-minimalism-enforced-mechanically): the developer-facing process surface is rendered as cards so adding or removing a script is visible work. Any alternative is measured against that, not just "does it run my build."

| Candidate | What it is | Outcome |
|---|---|---|
| `pi.dev` | Terminal AI coding-agent harness (Claude Code / Codex class) | **Different category** — alternative to the agent host, not to moondeck.py |
| VS Code `tasks.json` / JetBrains run configs | Editor-coupled command runners | **Complement, not substitute** — editor-specific, no editor-agnostic surface; hybrid pattern (tasks.json invokes `scripts/*.py`) keeps the single source of truth |
| `pio home` / PlatformIO IDE extension | Bundled-with-PlatformIO dashboard | **Insufficient** — covers pio commands; doesn't render custom scripts (mkdocs serve, classify_tests, scenario runs) → drift risk for everything outside pio |
| `just` / `Taskfile.dev` / `make` | CLI task runners with optional TUI pickers | **No surface visibility** — command palette out of sight by default; same v1 failure mode CLAUDE.md cites |
| `mprocs` / `process-compose` / `overmind` | Multi-process supervisors with TUI | **Shape mismatch** — for long-running processes (build watcher + serial + docs), not one-shot tasks; useful *alongside* moondeck.py if scope grows |
| Streamlit / Gradio / Marimo | Python → web UI frameworks | **Premature** — moondeck.py is small enough to stay hand-rolled; revisit when moondeck.py drift demands fewer LOC per card |
| `tmux` + shell scripts | Most minimalist; persistent panes via SSH | **Viable alternative** — drops the GUI; pure unix; perfect process visibility (every pane is a tab). Land if moondeck.py outgrows what one screen can show |
| WASM frontend (Yew / Leptos / Vugu) | Compile-to-WASM rendering of moondeck.py | **Overkill** — toolchain cost for a small UI; net negative under §1 |
| Compile firmware to WASM via Emscripten | Run the v2 light pipeline in a browser tab | **Orthogonal, future-interesting** — enables shareable demo URLs (Wokwi-style); doesn't replace moondeck.py's deploy role. Land if "click here to see the effect" becomes a felt need |
| WebSerial + `esptool-js` / **ESP Web Tools** | Browser flashes the device via the WebSerial API | **Future end-user path** — replaces the flash/monitor corner of moondeck.py for *external contributors*. Doesn't help the maintainer-side build/test loop. Land when v1→v2 cutover is public (Release 2) |
| **ESPConnect** ([repo](https://github.com/thelastoutpostworkshop/ESPConnect), [live](https://thelastoutpostworkshop.github.io/ESPConnect/)) | Polished Vue 3 + Vuetify + `tasmota-webserial-esptool` browser app; flash, backup, LittleFS/SPIFFS/FatFS/NVS browser | **Concrete reference for the WebSerial path** — proves the pattern at scale (1.8 k★, MIT). WASM appears in its bundle only as the esptool chip-stub blobs, not as the UI rendering technology. The LittleFS-from-browser feature is independently interesting for inspecting v2's `/state-*.json` on a flashed device |

**Decision:** **KEEP `scripts/moondeck.py`.** It's small, editor-agnostic, and the only candidate that renders the *project's own* custom scripts as a visible surface. Two artefacts are recorded for future activation:

- **End-user flash path via WebSerial** — land when external contributors arrive. Default reference is ESP Web Tools (Espressif-blessed `<esp-web-install-button>` component); ESPConnect is the polished bespoke version if more than flash is needed.
- **Firmware-in-WASM via Emscripten** — land when shareable effect-demo URLs become a felt need. Not Release 1 scope.

Both unlock under the same drift-episode rule as Sprint 8's [Deferred](#sprint-8-deferred) list: no episode → no promotion.

---

## Sprint 9 — Release 1 polish: minimalism review + deploy & docs read-through {#sprint-9}

> **Scope:** Sprints 1–8 built the foundation; Sprint 9 *reviews* it. No new features. Three workstreams, one sprint:
>
> 1. **Per-file minimalism review** of `src/`, `test/`, and the guardrails in `scripts/`. Walk each file with §1 and §4 in hand; delete what doesn't pay for itself; tighten guardrails where Sprint 4–8 hindsight surfaces a real drift class.
> 2. **Deploy process walk** via `scripts/moondeck.py`. Every card exercised end-to-end on a fresh clone. The Sprint 7 fatfs/PIO-Python collision is either fixed at the source or made the documented path — a new contributor on macOS with `brew install python` must run "Build esp32s3_n16r8" from `moondeck.py` without manual intervention.
> 3. **Documentation read-through** of `docs/`. Trim what no longer pays for itself; fix anchor drift left by the Minimalism rename; reconcile `process.md` / `system.md` with what actually shipped.
>
> **Bias:** this sprint removes more than it adds. Net LOC ≤ 0 across `src/` + `docs/` combined (the test-surface growth in Sprint 8 is intentional and stays); per-surface budget *tightening* is preferred over loosening. A green guardrail run is necessary but not sufficient — the read test is whether a new reader can hold the foundation in their head after one pass through `docs/`.

### Definition of Done

#### Minimalism review (source + test)

- [ ] Per-file walk of `src/` and `test/`. PR description carries a one-line outcome per file: `kept verbatim` / `trimmed N LOC` / `deleted`. "Nothing to strip" is a valid outcome when recorded with what was examined (per [process arch §4](../architecture/process.md#4-port-and-minimize-where-substantive-modules-come-from)).
- [ ] At least one substantive deletion lands. Per the [§3 anti-drift rule](../architecture/process.md#3-anti-drift-why-these-rules-survive) ("every release removes at least one thing"), the Sprint 9 commit is the natural place to honour that.
- [ ] Surface-level LOC budgets re-checked. Any surface where actual ≤ 50 % of budget gets the budget *tightened* in `scripts/check_loc.py` (with the post-trim count + ~10 % headroom). PR records the new numbers. Sprint 8's test budgets land at "post-write + 15 % headroom" and stay there.

#### Guardrails review (enforcement)

- [ ] Each `scripts/check_*.py` walked: what drift class does it catch, what did it miss across Sprints 4–8? A new check or refinement lands only when a real Sprint 4–8 incident motivates it. Concrete candidates from this release:
  - The `MALLOC_CAP_8BIT` IRAM-fault class — can a regex flag `heap_caps_malloc(..., MALLOC_CAP_INTERNAL[^|]*)` without the 8BIT bit?
  - Is the `printf` / `Serial.print` family in scope of `check_hot_path.py`'s banned list, given CLAUDE.md's existing in-prose ban on info-level logging in `loop*`?
  - Does Sprint 8's `[MemBoot]` / `[MemLive]` line-shape want its own format guard, or is the doctest replay's assertion enough?
- [ ] Pre-commit + CI green on the post-walk tree with the post-walk guardrails.
- [ ] Any new guardrail justifies itself per [process arch §2](../architecture/process.md#2-guardrails-minimalism-enforced-mechanically) in the PR description.

#### Deploy process walk

- [ ] Every card in `scripts/moondeck.py` exercised end-to-end on a fresh clone. Each gets a one-line "verified ✓ on $DATE" or "failed → fixed by $X" entry in `docs/deploy.md`.
- [ ] The fatfs / PlatformIO-Python collision is closed: either `scripts/_pio.py` resolves cleanly without manual `uv` invocation, or `docs/deploy.md` documents the `uv run --with platformio --with fatfs-ng ...` path prominently enough that a new contributor finds it on first build failure.
- [ ] Any `scripts/*.py` file that no longer maps to a `moondeck.py` card or a CI step is deleted.

#### Documentation read-through

- [ ] `docs/index.md`, `docs/architecture/{system,process}.md`, `docs/development/{index,release-01,release-02}.md`, every `docs/adr/*.md`, `docs/deploy.md`, `docs/lights.md` read end-to-end. Outcomes: link rot fixed, stale references updated or deleted.
- [ ] Anchor audit post-Minimalism rename: every cross-reference resolves; no broken anchors left behind by the renamed headings (`port-and-minimize`, etc.). MkDocs build green on links.
- [ ] `process.md` and `system.md` reconciled with what shipped. The `MALLOC_CAP_8BIT` rationale (today only in `PalHeap.h`'s header) is promoted to `system.md § Pal` if other Pal files would benefit from the same byte-store discipline. The `[MemBoot]` / `[MemLive]` line-format contract is documented in `system.md § Logger` (or wherever Sprint 8 places it).
- [ ] The renamed [Validated during Release 1](#validated-during-release-1) section reviewed: does its history value justify staying in `release-01.md`, or does the substance move into `process.md` and the section get deleted? Decide and act.

#### Release 1 closure

- [ ] `v1.0.0-foundation` tag created from the Sprint 9 head once all the above are green.
- [ ] One-paragraph retrospective added at the top of [Release 2](release-02.md): what the Release 1 baseline actually delivers, what Release 2 inherits, what Release 2 is *not* allowed to redo (per the [port-and-minimize §4](../architecture/process.md#4-port-and-minimize-where-substantive-modules-come-from) discipline applied recursively to v2 itself).

### Deferred

- [ ] Recurring-evaluation sprint (Release 5 per the [Release Overview](#release-overview)) — framing is set in Release 1; concrete scope earns its place when Release 4 wraps.
- [ ] Doc-growth budget number. Already DROPPED in [Validated during Release 1](#validated-during-release-1); revisit only if `docs/` drift becomes a felt problem.
- [ ] `healthReport()` meta-test. Already DROPPED — reconsider only when the test surface exceeds the "readable in one sitting" threshold the v2 stance relies on.

---

## Validated during Release 1

The [process architecture](../architecture/process.md) states the contract in high-level terms; the items below were Release-1 specifics inherited from v1's Release 9 guardrails outline. Each had to earn its place under the minimalism rule. Outcomes by Sprint 7:

**Tool choices for the three guardrail tiers** — **REPLACED.** None of v1's candidates (`clang-format`, `ruff`, `clang-tidy` with `bugprone-*`/`modernize-*`, `cppcheck`) were adopted. Each tier ships a purpose-built Python check in `scripts/`: `check_loc`, `check_hot_path`, `check_gpio`, `check_structure`, `check_platform_guards`, `check_bundle`. No formatter was added — mechanical formatting wasn't on v1's failure list. Pre-commit via `.githooks/pre-commit`; the same scripts run in `.github/workflows/ci.yml`.

**Hot-path enforcement mechanism** — **KEPT (regex/Python).** `scripts/check_hot_path.py` matches `void [Class::]loop*() {` with brace-balanced body extraction and a banned-pattern scan (no `new` / `malloc` / `psram_malloc` / `JsonDocument` / `delay` / `vTaskDelay` / `sleep` / `usleep` / `recv`). ~50 LOC. The clang-tidy plugin and AST options were not adopted — overkill for the surface.

**Footprint baseline format** — **REPLACED.** Budgets live inline in `scripts/check_loc.py`'s `BUDGETS = {...}` dict, each entry annotated with a rationale comment. Bumps are PR-visible inline edits to that dict. No separate `baselines/footprint.json` was created.

**Doc-growth budget number** — **DROPPED.** No automated count was added. Doc growth is judged at review time. Revisit only if drift becomes a felt problem.

**Structural-additions justification format** — **REPLACED.** Convention is top-of-file docstring (Python) or top-of-file `//` block (C++). Top-level directory additions additionally require an ADR, enforced by `scripts/check_structure.py`'s allowlist. No `// WHY:` marker syntax, no PR-template field.

**Verifier-of-the-verifier** — **REPLACED.** The "growth gated by the structural rule" half stayed and is now enforced by `check_structure.py`'s top-level allowlist. The `healthReport()` meta-test never landed and isn't needed — the test surface is 203 LOC across two files (`test/test_pc/test_module.cpp`, `test_http.cpp`), small enough to read in one sitting; a meta-assertion buys nothing at that size. (Updated by Sprint 8 Rail 1: surface grew to 9 test files / ~700 LOC / 34 cases; still readable in one sitting.)

### Artefact promotions {#artefact-promotions}

Each line records the promotion of a deferred test artefact from Sprint 8's [Deferred](#sprint-8-deferred) list. Format: `YYYY-MM-DD — promoted <artefact>. Drift episode: <one-line description of what slipped past the current rails>.` Without a drift-episode entry, no promotion lands. This is the [§3 anti-drift rule](../architecture/process.md#3-anti-drift-why-these-rules-survive) applied to test infrastructure itself.

- **2026-05-13** — promoted **REST scenario runner** (`scripts/scenario.py`). Drift episode: after Sprint 8 Rail 3 landed in-process replay and the post-Sprint-8 work added a discovered-Devices list to `scripts/moondeck.py`'s Live tab, there was no card that *used* the device list — the "Run scenarios" card still only exercised the maintainer's PC via `pio test`. The gap was visible: a device list with no live runner to consume it. Promotion ships `scripts/scenario.py` (replays each scenario JSON against `--host <h>` or `--all-enabled` from `moondeck.json`) and a `live-scenarios-devices` card in moondeck.py.

## Sprint 10 — MoonDeck: tabbed dev console + live device surface + REST scenarios + agent loop {#sprint-10}

> **Scope:** The script-UI grew into a real tool and earned a name. Five themes land together:
>
> 1. **Tabbed rearchitecture** of `scripts/moondeck.py` — flat card list → four panes (PC / ESP32 / Live / Develop). Collapses ten per-env ESP32 cards into four tab-scoped ones.
> 2. **Live tab Devices list** — persistent inventory of reachable projectMM v2 instances (PC binary + ESP32s on the LAN), with probe / discover / add / per-row enable.
> 3. **REST scenario runner** (`scripts/scenario.py`, promoted from [Sprint 8 deferred](#sprint-8-deferred)) — replays `test/test_pc/scenarios/*.json` against enabled devices.
> 4. **Agent loop** below the output panel — Analyze / Fix / Ask buttons + a Develop-tab task list (Reverse engineer sprint, Commit via agent). All four endpoints stream live via SSE.
> 5. **Tool naming**: `ui.py` → `moondeck.py`, `ui.json` → `moondeck.json`, header rebranded "MoonDeck", logo + favicon. Sweep across docs/scripts. Lightweight reframing of `docs/deploy.md` so MoonDeck has top billing without obscuring the underlying scripts.
>
> **Minimalism stance.** `scripts/test.py` lost 39 LOC of buffered-alignment code (replaced by line-by-line streaming — the classification badge moved to `scripts/classify_tests.py` in Sprint 8 Rail 1). `scripts/moondeck.py` grew substantially (478 → 1464 LOC, +986 net), justified by collapsing six separate ESP32 cards into one env-scoped Build + one Flash + one Flash-fs + one Monitor (four cards replacing ten), adding the Devices list that the promoted scenario runner consumes, landing the Develop tab (release/sprint navigation + agent-driven sprint authoring + commit-via-agent), and switching every agent endpoint from blocking JSON to SSE streaming so the user sees the agent's narration + tool calls live. `scripts/scenario.py` is a net addition (~210 LOC) unlocked by the drift episode in [Artefact promotions](#artefact-promotions): a device list with no live runner to consume it.

### Definition of Done

#### `scripts/moondeck.py` — tab rearchitecture

- [x] Four tabs: **PC** (build/test/run, six guardrail checks, gen-bundle, mkdocs), **ESP32** (tab-scoped env selector + port dropdown; Build, Flash firmware, Flash filesystem, Serial monitor), **Live** (Devices list + two scenario cards), **Develop** (release/sprint dropdowns + sprint-authoring agent task). Source: `scripts/moondeck.py`.
- [x] ESP32 tab collapses the former per-env card duplication: one `Build` card reads `envSelect.value` (`esp32dev` / `esp32s3_n16r8`) via `needs_env: True`; similarly for Flash/Flash-fs/Monitor. Ten cards → four. Port picker scoped to the ESP32 tab (was a global header element).
- [x] `?` help links on cards now open docs in the right-panel iframe (MkDocs serve required) instead of a new browser tab; middle-click/right-click still open a new tab via the preserved `href`. View-bar above the panel shows which content is loaded; **← Output** button returns to the live stdout stream.
- [x] **Agent bar** below the output panel: **Analyze** sends the current log to `claude -p` (fixed prompt; replies `OK` or `ISSUE: …`), **Fix** appears on `ISSUE` (asks agent to edit files — no commits, no pushes, confirm-gated), **Ask** sends a free-form question + log. All three require `claude` CLI on PATH. Source: `_do_agent()` in `scripts/moondeck.py`.
- [x] **Streaming agent invocations.** All four agent endpoints (`/analyze`, `/fix`, `/ask`, `/agent-task`) switched from blocking `subprocess.run(capture_output=True)` + JSON response to `subprocess.Popen` + SSE `line` events per stdout line. Frontend uses `fetch + ReadableStream` to parse the stream (POST + streaming — EventSource is GET-only). User sees claude's narration and tool calls live, same UX as a terminal run. Common helper: `_stream_claude_to_sse(prompt, timeout)`.
- [x] **Static asset handler** (`GET /assets/<name>`) — sandboxed to `docs/assets/` (no `..` traversal via `Path(name).name`), MIME table for png/jpeg/svg/ico/gif/webp, 1 h browser cache. Earns its place by serving the new logo + favicon; reusable for any future asset.
- [x] **Branding**: `docs/assets/moonlight-logo.png` (320×320 PNG, 23 KB) rendered top-left in the header (28×28), and used as favicon via `<link rel="icon">`. HTML title `MoonDeck — projectMM v2`, h1 `MoonDeck` with muted-grey `— projectMM v2` subtitle, startup banner `MoonDeck: http://127.0.0.1:8765/`.

#### Live — Devices list

- [x] Persistent device list with per-row enable/disable checkbox, name (clickable → loads device UI in the right-panel iframe), host:port, last-seen status, and remove button.
- [x] **Refresh** probes each known device's `GET /api/system`; **Discover** sweeps a configurable subnet (default `192.168.1.0/24` port `80`, 32-thread pool via `concurrent.futures`); **Add** takes manual `host[:port]`. Scan hits filtered by `chip_model` field presence to reject non-projectMM HTTP servers.
- [x] State persisted to `moondeck.json` at repo root (gitignored — dev-host-specific local-network IPs/MACs). Default entry: `PC (local Run card)` at `127.0.0.1:8080`. Server-side endpoints: `GET /ui-state`, `POST /ui-state`, `POST /probe`, `POST /scan`. Source: `load_ui_state()`, `save_ui_state()`, `probe_device()`, `scan_subnet()` in `scripts/moondeck.py`.
- [x] `.gitignore` updated: `moondeck.json` entry with rationale comment.

#### Develop tab

- [x] Release + Sprint dropdowns auto-populated from `docs/development/release-*.md` headings (`scan_releases()` in `scripts/moondeck.py`). Per-release sprint memory via `localStorage`. **Documentation** button loads the selected sprint anchor in the right-panel iframe.
- [x] **Reverse engineer sprint** card: sends a `claude -p` task (prompt in `DEV_TASKS` dict) that inspects `git status`/`diff`/`log`, reads the existing sprint format, and composes a ready-to-paste sprint section. Agent does NOT edit files or commit. Source: `DEV_TASKS["reverse-engineer-sprint"]` in `scripts/moondeck.py`.
- [x] **Commit via agent** card: creates a git commit for pending changes following the project's commit style (lowercase prefix, em-dash separator, body bullets, `Co-Authored-By` footer). Hard constraints baked into the prompt: no `git push`, no `git commit --amend`, no `--no-verify`, never `git add -A` / `git add .`, explicit skip-list for secret files (`.env`, `credentials*.json`, `wifi.json`, `moondeck.json`). Respects the existing staged set if any (commits only what's staged); refuses with an explanation if the diff spans unrelated topics. Source: `DEV_TASKS["commit-via-agent"]` in `scripts/moondeck.py`.
- [x] DEV_TASKS catalogue designed for extensibility: each entry exposes only `id` / `label` / `docs_anchor` to the frontend; full prompt stays server-side. Adding a task = one DEV_TASKS entry + one `### {label} {#anchor}` section in `docs/deploy.md`; the Develop tab renders one card per entry automatically.

#### `scripts/scenario.py` — REST scenario runner (promoted)

- [x] New file `scripts/scenario.py` (209 LOC). Replays `test/test_pc/scenarios/*.json` against a live device over REST: `POST /api/modules` for `add_module`, `POST /api/control` for `set_control`. `--host <h> --port <p>` for single target; `--all-enabled` iterates `moondeck.json` enabled devices.
- [x] Cleanup before/after each scenario: `DELETE` every module whose type is not in `HEAD_TYPES` (`system`, `wifi-sta`, `http`, `ws`, `state-store`). Measure steps sample `/api/system` + `/api/modules` and assert `bounds.module_count.{min,max}`.
- [x] `docs/development/release-01.md` Sprint 8 deferred entry marked `[x]` with `PROMOTED 2026-05-13`; [Artefact promotions](#artefact-promotions) entry added recording the drift episode.

#### `scripts/test.py` — streaming simplification

- [x] Removed 39 LOC of `_parse()` / `_align()` / `_TYPE` buffered-alignment code. Output now streams line-by-line as PIO produces it — the moondeck.py log window is no longer blank for the first 5 s of a test run. Classification badge available via `| uv run scripts/classify_tests.py` pipe.

#### Naming: rename to MoonDeck

- [x] `scripts/ui.py` → `scripts/moondeck.py` (git mv); `ui.json` → `moondeck.json` (local, gitignored). Sweep across 11 files: docs/scripts/.gitignore/pyproject.toml. Zero residual `ui.py` / `ui.json` strings.
- [x] Tab labels finalised: `Live Tests` → `Live`, `Development` → `Develop`. `data-tab` attribute values (`live`, `dev`) and anchor IDs (`#live-devices`, `#live-scenarios-devices`) unchanged — no broken inbound links.
- [x] `feat(ui):` commit-prefix example in the `commit-via-agent` prompt updated to `feat(moondeck):` so future agent-driven commits use the new component name.
- [x] **Why the name.** Two readings of "deck": a *deck of cards* (the UI is literally cards) and a *flight deck* (control). Brand-consistent with MoonModules / MM / Minimalism family. See conversation log for the deliberation.

#### `docs/deploy.md` — tab-aware rewrite + MoonDeck reframing

- [x] Rewritten to match the four-tab layout: per-tab scope descriptions, new sections for [Devices](../deploy.md#live-devices), [Run scenarios (live)](../deploy.md#live-scenarios-devices), [Reverse engineer sprint](../deploy.md#reverse-engineer-sprint), [Commit via agent](../deploy.md#commit-via-agent), and the agent loop (Analyze / Fix / Ask paragraph in the MoonDeck section). ESP32 card docs updated from per-env headings to single-card + env-selector references. USB serial port picker docs scoped to the ESP32 tab.
- [x] **Lightweight reframing for MoonDeck identity**: H2 `## Interactive: the script UI {#interactive}` → `## MoonDeck — interactive dev console {#moondeck}`. New intro paragraph names MoonDeck as the primary interactive surface while preserving the doc's broader scope (the same scripts run from the shell — what CI and pre-commit do). Inbound `#interactive` links in release-01.md updated to `#moondeck`.
- [x] Stale screenshot link `assets/ScriptUI.png` → `assets/moondeck.png` (the renamed screenshot in `docs/assets/`).

#### `docs/development/release-01.md` — post-Sprint-8 additions

- [x] [Tools investigation](#sprint-8-tools-investigation) subsection added under Sprint 8: evaluation of 11 orchestration alternatives to `scripts/moondeck.py` with decision rationale. Outcome: **KEEP `scripts/moondeck.py`**; two future artefacts recorded (WebSerial end-user flash path, firmware-in-WASM).

### Deferred

- [ ] `scripts/scenario.py` bounds beyond `module_count` (e.g. `fps_min`, `heap_free_min`) — unlocks per the same drift-episode rule: when a numeric regression slips past the current `module_count`-only assertion.
- [ ] Devicelist parallel orchestration — unlocks when more than one device is tested every PR.
- [ ] `scan_releases()` file-watcher — currently re-scanned at process start only; restart moondeck.py to pick up new releases/sprints. Unlocks when the restart friction is felt.