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

---

## Sprints

| Sprint | Goal | Frugality target |
|--------|------|------------------|
| [Sprint 1](#sprint-1) | Guardrails framework + empty Module/Manager/Scheduler/Pal skeleton + Linux PC CI green | core ≤ 300 LOC |
| [Sprint 2](#sprint-2) | First module + module factory + REST + control panel + host tests (PC only) | tests ≤ 5 files |
| [Sprint 3](#sprint-3) | Pal-minimum + ESP32 builds green + on-target tests + HIL | Pal ≤ 200 LOC |
| [Sprint 4](#sprint-4) | WiFi-STA + WebSocket + REST over hardware; v1 frontend connects unchanged | per-module ≤ 300 LOC |
| [Sprint 5](#sprint-5) | Light domain: producer → SPSC ring → consumer; one effect, one preview driver | per-module ≤ 200 LOC |
| [Sprint 6](#sprint-6) | ArtNet, OTA, NTP, state persistence — parity with v1 first-boot pipeline | per-module ≤ 300 LOC |

Each sprint closes with a working device. After Sprint 6, v1 is tagged `v1.8.x-legacy`, this repo is renamed from `projectMM-v2` to `projectMM`, and v2 ships its first stable tag `v2.0.0`.

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

## Sprint 2 — First module, REST, control panel, host tests {#sprint-2}

> **Scope:** Reach a testable system on PC. A first module type, a module factory, an HTTP server module, REST endpoints for module add / remove / list, a browser page that uses them, host unit tests, and integration tests against the in-process server. PC only — ESP32 and HIL move to Sprint 3. This is the sprint that opens the static + dynamic test loop.

### Definition of Done

- [ ] `HelloModule` — `Module` subclass with one control and a counter
- [ ] Module factory in `ModuleManager`: `add(type, id)` constructs by type via a typename → factory registry (no `strcmp` chain)
- [ ] `HttpServerModule` (PC stdlib) serves the REST API and a small browser page
- [ ] REST: `GET /api/modules` (list), `POST /api/modules` (add by type), `DELETE /api/modules/{id}` (remove), `GET /api/modules/{id}` (read controls)
- [ ] Browser page served by `HttpServerModule`: list modules, add by type, remove by id — talks to the same REST
- [ ] Host unit tests via doctest, single binary; tests ≤ 5 files
- [ ] Integration tests: start the server in-process, hit REST, assert
- [ ] Test-count baseline locked at sprint close; bump rule enforced

---

## Sprint 3 — Pal-minimum + ESP32 + on-target tests {#sprint-3}

> **Scope:** `Pal` for timing, GPIO (via typed board config), filesystem basics, RTOS task and semaphore primitives, heap query. Nothing else — WiFi / Ethernet / UDP / OTA / NTP / system-info dumps do **not** live in `Pal`. esp32dev + esp32s3_n16r8 builds green; the Sprint 2 stack runs on hardware over serial (WiFi lands in Sprint 4). On-target unit tests and HIL probe land here against real hardware.

### Definition of Done

- [ ] Pal total ≤ 200 LOC across all platforms
- [ ] esp32dev and esp32s3_n16r8 builds green; Linux PC build still green
- [ ] Typed board config (`board/<env>.yaml` → `Pins.hpp` codegen) compiles for both ESP32 envs
- [ ] On-target unit tests run on ESP32 for modules where platform behaviour differs
- [ ] HIL probe verifies serial output on a connected device (skipped in CI without hardware)

---

## Sprint 4 — WiFi-STA + WebSocket + REST over hardware {#sprint-4}

> **Scope:** `WifiStaModule` connects on boot. The Sprint 2 HTTP / REST stack runs over WiFi on the device. `WebSocketModule` pushes schema and state in the v1 wire format. The v1 frontend connects to v2 hardware unchanged.

### Definition of Done

- [ ] `WifiStaModule` connects on boot using credentials from state
- [ ] HTTP + REST from Sprint 2 work on ESP32 over WiFi
- [ ] `WebSocketModule` pushes schema and state in v1 wire format
- [ ] REST endpoints `/api/modules`, `/api/system`, `/api/log` reach parity with v1
- [ ] v1 frontend connects to v2 hardware unchanged (list / add / remove modules + live state)
- [ ] Per-module footprint ≤ 300 LOC

---

## Sprint 5 — Light domain {#sprint-5}

> **Scope:** Port the light pipeline as modules. One producer (effect), one consumer (preview driver), SPSC ring buffer between them at depth 2. `RGB`, `pixelBuf`, and the producer / consumer base classes live in `modules/lights/`, not in core. The DAG declaration API (`scheduler.connect(producer, consumer)`) is exercised by this sprint.

### Definition of Done

- [ ] `EffectBase` and `DriverBase` in `modules/lights/`
- [ ] At least one effect (`SineEffect` or similar) producing a frame
- [ ] At least one preview driver consuming it and exposing pixels via the Sprint 4 WebSocket
- [ ] SPSC ring buffer primitive in core, depth 2, ESP32 + PC
- [ ] PreviewModule frame rate matches v1 on the same hardware
- [ ] Per-module footprint ≤ 200 LOC

---

## Sprint 6 — Parity sprint {#sprint-6}

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
