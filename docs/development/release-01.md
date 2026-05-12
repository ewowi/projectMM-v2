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
| [Sprint 6](#sprint-6) | Light domain: producer → SPSC ring → consumer; one effect, one preview driver | per-module ≤ 200 LOC |
| [Sprint 7](#sprint-7) | ArtNet, OTA, NTP, state persistence — parity with v1 first-boot pipeline | per-module ≤ 300 LOC |

Each sprint closes with a working device (or working PC application for sprints 2–3). After Sprint 7, v1 is tagged `v1.8.x-legacy`, this repo is renamed from `projectMM-v2` to `projectMM`, and v2 ships its first stable tag `v2.0.0`.

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

- [ ] Copy v1's `src/core/WsServer.h` verbatim into `src/pal/PalWs.h` (platform-conditional → lives in `pal/`); frugalize
- [ ] Wrap as `WebSocketModule` in `src/modules/network/` — platform-neutral, no `#ifdef`s; registers handshake on `/ws` via `pal::HttpServer`, pushes schema on connect, broadcasts state changes (driven by control updates and `loop1s`)
- [ ] REST mutations land: `POST /api/modules` (add), `DELETE /api/modules/{id}` (remove), `PATCH /api/modules/{id}` (set controls) — all via `HttpServerModule` dispatching into the control system

#### Step 3: `SystemStatusModule` (the first real `MoonModule`)

- [ ] Port v1's system-info `pal::*` accessors into `src/pal/PalSystemInfo.h`: `chip_model`, `mac_address`, `reset_reason_str`, `sketch_kb`, `total_heap_kb`, `total_psram_kb`, `fs_total_kb`, `fs_used_kb`, `platform_version`. Platform conditionals live in the pal file.
- [ ] Copy v1's `src/modules/system/SystemStatus.h` verbatim into `src/modules/system/SystemStatusModule.h`; frugalize
- [ ] `SystemStatusModule` inherits `MoonModule`, calls `addControl(...)` in `setup()` for `heap_used_kb`, `heap_free_kb`, `uptime_s`, `fps`, `local_time`, `chip_model`, `reset_reason`, etc.; updates fields in `loop1s`. **Zero platform conditionals** (enforced by `check_platform_guards.py`).
- [ ] `src/modules/system` LOC ≤ 300; `src/pal/PalSystemInfo.h` LOC ≤ 200
- [ ] `main.cpp` swaps `mm.add("hello", ...)` → `mm.add("system", "system-0")`; `src/modules/hello/` deleted (mandatory subtraction for Sprint 3)

#### Step 4: Frontend sources

- [ ] Port v1's `src/frontend/index.html` (84 LOC), `style.css` (744 LOC), `app.js` (1647 LOC) verbatim
- [ ] Port v1's `scripts/gen_frontend_bundle.py`; add a "Bundle" card to `scripts/ui.py` that regenerates `src/frontend/frontend_bundle.h` from the sources
- [ ] **Frugalize the frontend** under a UI-specific corollary of §4: strip UI for features v2 does not yet ship (lighting-domain controls, OTA upload screens, etc.). Future sprints add the UI for the features they introduce.
- [ ] Regenerated `frontend_bundle.h` is committed; CI's "Bundle" check (added with this work) verifies the bundle matches a fresh regeneration from the sources — drift between sources and bundle fails CI.
- [ ] **End-of-sprint verification:** open `http://127.0.0.1:8080`, see live SystemStatusModule with ticking uptime, heap, fps; controls render from schema; WebSocket "connected" indicator green; the served bundle is smaller than v1's because lighting-domain UI was stripped.

---

## Sprint 4 — Pal-minimum + ESP32 builds + on-target tests {#sprint-4}

> **Scope:** Fill in the remaining `src/pal/` files with their ESP32 implementations: `PalGpio.h`, `PalFs.h`, `PalRtos.h`, `PalHeap.h`. The Sprint 2/3 pal files (`PalHttp.h`, `PalWs.h`, `PalSystemInfo.h`) already have their ESP32 branches from the v1 port — Sprint 4 just adds the build envs and link dependencies. ESP32 builds green; the Sprint 3 stack compiles unchanged because every platform conditional has lived in `src/pal/` all along.

### Definition of Done

- [ ] New pal files: `PalGpio.h`, `PalFs.h`, `PalRtos.h`, `PalHeap.h` — each platform-conditional inside, each ≤ its [LOC budget](../deploy.md)
- [ ] `ESP32Async/ESPAsyncWebServer` added to `lib_deps` for ESP32 envs only (already required by `PalHttp.h`'s ESP32 branch)
- [ ] esp32dev and esp32s3_n16r8 envs added to `platformio.ini`; both builds green; Linux PC build still green; CI runs all three
- [ ] `PalSystemInfo.h`'s ESP32 branch lights up: real `chip_model`, `mac_address`, `reset_reason`, `sketch_kb` on hardware
- [ ] `check_platform_guards.py` still passes — adding ESP32 implementations means adding pal files, not adding `#ifdef`s anywhere else
- [ ] Typed board config (`board/<env>.yaml` → `Pins.hpp` codegen) compiles for both ESP32 envs
- [ ] On-target unit tests run on ESP32 for at least one module where platform behaviour differs (e.g. SystemStatusModule heap reads)
- [ ] HIL probe verifies serial output on a connected device (skipped in CI without hardware)

---

## Sprint 5 — WiFi-STA + REST + WebSocket over hardware {#sprint-5}

> **Scope:** `WifiStaModule` connects on boot. The Sprint 3 HTTP / REST / WebSocket stack runs over WiFi on the device. The Sprint 3 frontend connects to v2 hardware unchanged — same wire format, no frontend changes.

### Definition of Done

- [ ] `WifiStaModule` connects on boot using credentials from state
- [ ] HTTP + REST + WebSocket from Sprint 3 work on ESP32 over WiFi
- [ ] Sprint 3 frontend connects to v2 hardware unchanged (all controls live, add/remove works)
- [ ] REST endpoints `/api/modules`, `/api/system`, `/api/log` reach parity with v1
- [ ] Per-module footprint ≤ 300 LOC

---

## Sprint 6 — Light domain {#sprint-6}

> **Scope:** Port the light pipeline as modules. One producer (effect), one consumer (preview driver), SPSC ring buffer between them at depth 2. `RGB`, `pixelBuf`, and the producer / consumer base classes live in `modules/lights/`, not in core. The DAG declaration API (`scheduler.connect(producer, consumer)`) is exercised by this sprint.

### Definition of Done

- [ ] `EffectBase` and `DriverBase` in `modules/lights/`
- [ ] At least one effect (`SineEffect` or similar) producing a frame
- [ ] At least one preview driver consuming it and exposing pixels via the Sprint 5 WebSocket
- [ ] SPSC ring buffer primitive in core, depth 2, ESP32 + PC
- [ ] PreviewModule frame rate matches v1 on the same hardware
- [ ] Per-module footprint ≤ 200 LOC

---

## Sprint 7 — Parity sprint {#sprint-7}

> **Scope:** ArtNet (in + out), OTA firmware update, NTP wall-clock, LittleFS state persistence. Sprint closes when the v1 first-boot pipeline runs identically on v2 hardware.

### Definition of Done

- [ ] `ArtNetInModule`, `ArtNetOutModule` ship, wired by `autoWireKeys()`
- [ ] `FirmwareUpdateModule` accepts file upload and GitHub-release flashing
- [ ] `NtpModule` syncs and exposes `local_time`
- [ ] State persistence via LittleFS modules
- [ ] First-boot pipeline runs identically on v2 hardware (visual + metrics parity check against v1)
- [ ] v1 tagged `v1.8.x-legacy`; this repo renamed from `projectMM-v2` to `projectMM`; v2 tagged `v2.0.0`

---

## To be validated during Release 1

The [process architecture](../architecture/process.md) states the contract in high-level terms. The items below are specifics inherited from v1's Release 9 guardrails outline that need to earn their place under the frugality rule (does this addition pay for itself?) before they are codified. Each is a sprint-time decision in Release 1, not a Release-1 entry condition.

**Tool choices for the three guardrail tiers.** Specific formatters, linters, and static analysers — for example `clang-format`, `ruff`, `clang-tidy` with `bugprone-*`/`modernize-*`, `cppcheck`. Each must justify itself or be dropped. The *rules* are fixed by process architecture; the *tools* are not.

**Hot-path enforcement mechanism.** Whether hot-path bans (no allocations, no blocking calls, no logging in `loop*()` bodies) are enforced by regex over `void <name>::loop\b.*\{` … matching `}`, by a clang-tidy plugin, by AST tooling, or by something else. Whatever it is must be cheap to maintain.

**Footprint baseline format.** Where module footprint baselines live (`baselines/footprint.json` was the v1 proposal) and what triggers an update — PR description entry, signed-off bump line, or another mechanism.

**Doc-growth budget number.** v1 proposed 500 lines per release; v2 picks a number when it has enough data to pick one.

**Structural-additions justification format.** Whether `// WHY:` (C++) and a top-of-file Python docstring are the right way to record why a new file in `scripts/`, `tests/`, or `docs/` exists, or whether a single mechanism (e.g. PR template field) is cheaper.

**Verifier-of-the-verifier.** v1's answer was: the testing system's growth is gated by the structural rule, and its correctness is asserted only by the unit test that every module's `healthReport()` is non-empty — no meta-tests. v2 inherits this stance by default but should validate it once `healthReport()` exists.

Each item closes with a one-line outcome in this section by Sprint 6 (kept, dropped, or replaced). What is kept moves into [process architecture](../architecture/process.md); what is dropped disappears.
