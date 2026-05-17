# Release 1 — Restart to Parity

Bring v2 to parity with v1's first-boot pipeline — effect → blend → driver → preview, served over HTTP / WS with WiFi-STA, persisted to LittleFS — implemented as modules over a small core. The decision to restart from v1 is documented in v1's [Release 9](https://ewowi.github.io/projectMM/development/release-09/). The contract under which this release executes is [process architecture](../architecture/process.md): minimalism, guardrails, anti-drift. Items deferred from any sprint live in [backlog.md](backlog.md).

## What Release 1 delivers

A v2 codebase that runs v1's first-boot pipeline as modules over a small core: [`MoonModule`](../architecture/system.md#moonmodule-the-contract) + [`ModuleManager`](../architecture/system.md#modulemanager-instance-ownership) + [`Scheduler`](../architecture/system.md#scheduler-dag-runner-across-cores) + a minimal [`Pal`](../developer-guide/pal.md). Networking, persistence, the HTTP / WS server, and the entire lighting domain are modules. The deploy surface is [MoonDeck](../developer-guide/deploy.md#moondeck) + a handful of scripts.

CI-enforced minimalism budgets are the load-bearing constraint: core ≤ 300 LOC, pal files capped individually ([pal inventory](../developer-guide/pal.md)), per-module 200–300 LOC depending on domain. Overshoot fails CI; bumps require an explicit signed-off edit to `scripts/check_loc.py`. Data sharing between modules is [one shared buffer, multiple zero-copy readers](../architecture/system.md#hot-path-data-sharing-between-modules).

### Mid-release pivot: port-and-minimize (2026-05-12)

Sprint 2 and Sprint 3 were initially attempted as greenfield rewrites of v1's HTTP server, WebSocket server, and frontend. The result was buggy distillations of code v1 had already debugged. The first attempts were deleted; subsequent sprints are rewritten around *porting* v1's working code and minimizing it. The discipline is codified in [process architecture §4](../architecture/process.md#4-port-and-minimize-where-substantive-modules-come-from).

---

## Sprints

| # | Goal | Detail |
|---|------|--------|
| [1](#sprint-1) | Guardrails framework + empty `Module` / `Manager` / `Scheduler` / `Pal` skeleton + Linux PC CI green | [process §2](../architecture/process.md#2-guardrails-minimalism-enforced-mechanically), [system](../architecture/system.md) |
| [2](#sprint-2) | Port `HttpServer` + v1 frontend bundle; UI shell visible at `:8080` | [HttpServerModule](../user-guide/network/http-server.md), [pal/PalHttp](../developer-guide/pal.md), [ADR 0001](../developer-guide/adr/0001-vendor-cpp-httplib.md) |
| [3](#sprint-3) | `MoonModule` (merge controls + lifecycle), `WsServer`, `SystemStatusModule`, frontend sources | [MoonModule contract](../architecture/system.md#moonmodule-the-contract), [WebSocketModule](../user-guide/network/web-socket.md), [SystemStatusModule](../user-guide/system/system-status.md), [ADR 0002](../developer-guide/adr/0002-arduinojson-lib-deps.md) |
| [4](#sprint-4) | ESP32 build envs + `PalSystemInfo` on hardware + HIL probe | [pal inventory](../developer-guide/pal.md), [SystemStatusModule](../user-guide/system/system-status.md) |
| [5](#sprint-5) | WiFi-STA + REST + WebSocket over hardware; frontend connects unchanged | [WifiStaModule](../user-guide/system/wifi-sta.md), [HttpServerModule](../user-guide/network/http-server.md), [WebSocketModule](../user-guide/network/web-socket.md) |
| [6](#sprint-6) | Light domain foundation + LittleFS state persistence | [RipplesEffect](../user-guide/lights/ripples-effect.md), [PreviewModule](../user-guide/lights/preview.md), [ArtnetOutModule](../user-guide/lights/artnet-out.md), [StateStoreModule](../user-guide/system/state-store.md) |
| [7](#sprint-7) | Two-core + PSRAM scaling: PalRtos, PalHeap, FrameRing SPSC, stress 128×128 on s3 | [RipplesEffect](../user-guide/lights/ripples-effect.md), [ArtnetOutModule](../user-guide/lights/artnet-out.md), [pal inventory](../developer-guide/pal.md) |
| [8](#sprint-8) | Test foundation: classified unit tests, `[MemBoot]` / `[MemLive]` events, in-process scenarios | (test surface in `test/test_pc/`) |
| [9](#sprint-9) | Release 1 polish: per-file minimalism review, deploy walk, docs read-through; tag `v1.0.0-foundation` | net LOC ≤ 0 |
| [10](#sprint-10) | MoonDeck — tabbed dev console + live device surface + REST scenarios + agent loop | [Deploy → MoonDeck](../developer-guide/deploy.md#moondeck) |
| [11](#sprint-11) | Docs restructure: four top-level sections (User Guide / Architecture / Developer Guide / Development), agent-memory framing in CLAUDE.md, MoonModule contract update, Pal inventory page | [docs](../index.md), [CLAUDE.md](https://github.com/ewowi/projectMM-v2/blob/main/CLAUDE.md) |
| [12](#sprint-12) | Minimalism pass: MoonModule field reorder (136B→96B), `type_` as `const char*`, typed `addControl` overloads, PSRAM-backed PreviewModule, RingBuffer heap accounting, class-size checker, `max_alloc_kb` in status bar | `src/core/`, `src/modules/`, `scripts/check_class_sizes.py` |
| [13](#sprint-13) | Shared data ring: `DataRing<T>` + `DataRegistry` in core; zero-copy producer/consumer pixel pipeline; removes `FrameRing`, `PixelRegistry`, and `PreviewModule` staging buffer; depth 1 on esp32dev, 2 on S3 | `src/core/DataRing.h`, `src/core/DataRegistry.h`, `src/modules/lights/` |
| [14](#sprint-14) | Ring → single-slot buffer; PATCH: convention; script LOC budgets; `frontend.md`; doc crosslink pass | `src/core/DataBuffer.h`, `scripts/check_patches.py`, `docs/developer-guide/` |
| [15](#sprint-15) | GridLayoutModule: geometry flow + serpentine wiring; effects become geometry-agnostic | `modules/lights/GridLayoutModule.h`, `RipplesEffect` geometry controls removed |
| [16](#sprint-16) | Module tree drag & reparent: cross-level drag-drop, auto-wire, loop order, layer slot | `src/core/`, `src/modules/network/HttpServerModule.cpp`, `src/frontend/app.js` |

The v1 → v2 cutover (rename + final stable tag) closes [Release 2](backlog.md#release-2-v1-parity-cutover), which adds ArtNet **in**, OTA, NTP, and any remaining v1 parity bits.

---

## Sprint 1 — Guardrails and skeleton {#sprint-1}

The minimum guardrails framework that the empty `Module` / `Manager` / `Scheduler` / `Pal` skeleton justifies — no more. Four lifecycle cadences (`loop`, `loop20ms`, `loop1s`, `loop10s`) as first-class scheduler concerns from commit 1, not afterthoughts. Linux-PC CI green; macOS / Windows / ESP32 envs land when those platforms gain real code. Pre-commit hook + CI gates active for raw-GPIO ban, hot-path allocation ban, hot-path blocking-call ban, structural-additions allowlist, LOC budget. Per-script `moondeck.py` cards from day one — see [Deploy → MoonDeck](../developer-guide/deploy.md#moondeck).

v1's Release 9 proposed specific tools for each guardrail tier; each was evaluated against the minimalism rule before adoption: **tool choices** — none of v1's candidates (`clang-format`, `ruff`, `clang-tidy`, `cppcheck`) adopted; each tier ships a purpose-built Python check in `scripts/check_*.py` instead. **Hot-path enforcement** — kept as regex/Python (`check_hot_path.py`, ~50 LOC); clang-tidy/AST rejected as overkill. **Footprint baseline format** — budgets inline in `check_loc.py`'s `BUDGETS` dict, PR-visible; no separate `baselines/footprint.json`. **Structural-additions justification** — top-of-file docstring (Python) or `//` block (C++); top-level dirs require an ADR. **Verifier-of-the-verifier** — `check_structure.py` covers the structural half; `healthReport()` meta-test never landed (test surface readable in one sitting). Dropped outcomes are in [backlog.md → Parking lot](backlog.md#parking-lot).

## Sprint 2 — Port `HttpServer` + serve v1 UI shell {#sprint-2}

First attempt: greenfield HTTP server module — hit the bug classes v1 had already debugged (TCP fragmentation, threading races, body-parsing edge cases). Pivot: port v1's `HttpServer.h` verbatim into `src/pal/PalHttp.h` (it carries the only platform conditional; module code gets the abstraction not the conditional) — see [ADR 0001](../developer-guide/adr/0001-vendor-cpp-httplib.md) for vendoring cpp-httplib. New guardrail `scripts/check_platform_guards.py` rejects `#ifdef ARDUINO` outside `src/pal/`. [HttpServerModule](../user-guide/network/http-server.md) serves the gzipped v1 SPA bundle at `/` (PC only this sprint — ESP32 lands in Sprint 4).

The minimization step found **no patches over symptoms** in the v1 verbatim port — every odd-looking branch turned out to be a deliberate architecture decision (graceful ESP32 503 on `bad_alloc`, cross-platform regex/glob adapter, ESPAsyncWebServer chunked-body buffering). LOC unchanged from v1 (332).

## Sprint 3 — `MoonModule` + `WsServer` + `SystemStatusModule` {#sprint-3}

Three substantive ports together because they validate each other.

**Step 1 — `MoonModule`.** Merge v2's lifecycle (`Module`) with v1's control system (`StatefulModule`) into one class — see [MoonModule contract](../architecture/system.md#moonmodule-the-contract). v1's 875-LOC inline header → 450 LOC split across `.h` + `.cpp` (51%). Five refinements beyond a 1:1 port: factory-injected `classSize` (replaces v1's CRTP); `onAllocateMemory()` generalizes v1's lighting-only `onSizeChanged`; `onBuildControls()` replaces v1's `rebuildControls()` duplication; `dynamicMemorySize()` derived from a single cached value (no separate `heapSize()` to drift); single-method public API per concern. ArduinoJson via `lib_deps` — see [ADR 0002](../developer-guide/adr/0002-arduinojson-lib-deps.md).

**Step 2 — WebSocket transport.** Port v1's `WsServer.h` into `src/pal/PalWs.h` (483 → 247 LOC, ~51%). Wrap as [WebSocketModule](../user-guide/network/web-socket.md) on port 81 (cpp-httplib has no WebSocket support; HTTP-upgrade would force a second HTTP library). REST mutations on [HttpServerModule](../user-guide/network/http-server.md): `POST /api/modules`, `DELETE /api/modules/{id}`, `PATCH /api/modules/{id}`. Bug caught + fixed: `ModuleManager::add()` called `m->setup()` directly instead of `m->runSetup()`, silently skipping `onBuildControls`; both `add()` / `remove()` now use the `run*` dispatch wrappers.

**Step 3 — `SystemStatusModule`.** First real `MoonModule` — see [SystemStatusModule](../user-guide/system/system-status.md). PC stubs for `pal::PalSystemInfo`; real ESP32 values land Sprint 4. Wire-format alignment caught at end of sprint: v2 emitted `{event,modules}` envelopes; v1's frontend dispatches on `msg.t` and expects raw top-level arrays. Port-and-minimize default applied — use v1's working design, don't reinvent.

**Step 4 — Frontend sources.** Port v1's `index.html` / `style.css` / `app.js` byte-identical. Generator made *deterministic* (`gzip mtime=0`) so identical sources produce identical bytes — drift check via `scripts/check_bundle.py` is now meaningful. End-of-sprint browser test surfaced three missing handlers (`GET /api/types`, `getSchema` name field, `POST /api/modules/reorder`) — all fixed.

## Sprint 4 — ESP32 build envs + PalSystemInfo on hardware {#sprint-4}

Prove "every platform conditional has lived in `src/pal/` all along" — the load-bearing claim Sprints 2 + 3 made. Add `esp32dev` and `esp32s3_n16r8` envs; wire `ESPAsyncWebServer` to ESP32 envs only; light up `PalSystemInfo.h`'s ESP32 branch with real chip / heap / PSRAM / flash / reset-reason values (see [SystemStatusModule](../user-guide/system/system-status.md)). LDF mode `chain+` → plain `chain` to dodge the pioarduino Network-library-ordering trap. `main.cpp` defines `setup()` + `loop()` + `int main()` without a single `#ifdef`. **Scope deliberately reduced**: `PalFs.h` / `PalGpio.h` / `PalRtos.h` / `PalHeap.h` deferred to the sprints that introduce their first consumers — landing pal files ahead of their callers is the v1 anti-pattern Rule #1 forbids.

## Sprint 5 — WiFi-STA + REST + WebSocket over hardware {#sprint-5}

[WifiStaModule](../user-guide/system/wifi-sta.md) connects on boot via `/wifi.json` (LittleFS). `PalFs.h` (72 LOC) lands here with its first consumer; `PalWifi.h` (59 LOC) wraps WiFi-STA primitives. **Smart TX-power adaptation** finds the highest working level instead of jumping to the floor (19.5 → 17 → 15 → 13 → 11 → 8.5 dBm on timeout); hourly probe-back-up to detect improved conditions. `pal::HttpServer::begin()` / `WsServer::begin()` made idempotent so the Sprint 3 listeners self-start once the netif is up — Sprint 4's "deferred listener startup" path is gone. **Scheduler ESP32 fix**: arduino-esp32's `std::thread` maps to pthread with a ~3 KB default stack — too small for `Scheduler::core_loop`'s mutex + JSON dispatch (Double exception on entry). Fix: core 0 runs inline on the calling thread (`loopTask` has 8 KB); multi-core lands Sprint 7 with `PalRtos`'s explicit stack sizing.

## Sprint 6 — Light domain foundation + state persistence {#sprint-6}

Three lighting modules at full v1 parity: [RipplesEffect](../user-guide/lights/ripples-effect.md) produces frames, [PreviewModule](../user-guide/lights/preview.md) ships them to the frontend as binary WS frames, [ArtnetOutModule](../user-guide/lights/artnet-out.md) packs Art-Net OpDmx over UDP. Cross-module sharing via a tiny `PixelRegistry` (see [RipplesEffect → Pixel-buffer sharing](../user-guide/lights/ripples-effect.md#pixel-buffer-sharing-why-a-registry-not-dynamic_cast) for the design rationale). `PalUdp.h` (58 LOC) lands here with its first consumer. **Minimalism stance**: no parent modules, no producer/consumer base classes, no SPSC ring, no PSRAM allocator, no per-module core affinity, no effect layering, no FastLED driver — the data flow lives in three lighting modules + four headers in `modules/lights/`.

[StateStoreModule](../user-guide/system/state-store.md) reads `/modules.json` + `/state-<id>.json` on boot to rebuild the user's module configuration; saves the same every 10 s on diff. A device survives reboots with all user-added modules and per-control values restored.

## Sprint 7 — Two-core + PSRAM scaling {#sprint-7}

Scale the Sprint 6 pipeline to 128×128 by moving [ArtnetOutModule](../user-guide/lights/artnet-out.md) onto core 1 with an SPSC ring across cores, allocating effect buffers from PSRAM when available, and re-enabling multi-core scheduling on ESP32. `PalRtos.h` (35 LOC, `task_create_pinned`) and `PalHeap.h` (24 LOC, `psram_alloc` / `psram_free`) land with their first consumers.

**MALLOC_CAP_8BIT incident** — first cut omitted the cap; on classic esp32 the `MALLOC_CAP_INTERNAL` fallback could hand back an IRAM-region buffer that only supports 32-bit-aligned word access. First byte-store crashed with `LoadStoreError`. Pinning `MALLOC_CAP_8BIT` on both paths eliminates the fault class — see [PalHeap.h](https://github.com/ewowi/projectMM-v2/blob/main/src/pal/PalHeap.h)'s header comment.

**RipplesEffect perf tune** — per-pixel `sqrt + cos + HSV→RGB` fell over at 128×128 (~4 fps). Replaced with two precomputed `w·h` tables (Q16 phase offset + base-color RGB) + a 256-entry cos LUT. Inner loop: one Q16 subtract, one LUT load, three uint8 mul-shifts. Result: 4.5 → ~1700 fps at 128×128 (≈ 375×). See [RipplesEffect](../user-guide/lights/ripples-effect.md) for the developer reference.

HIL verified on both `esp32dev` (no PSRAM — alloc-failure path exercised cleanly at 128×128, system continues) and `esp32s3_n16r8` (8 MB PSRAM — full 128×128 with 97-universe Art-Net stream visually confirmed at 192.168.1.70).

## Sprint 8 — Test foundation: classified units + MemBoot/MemLive + in-process scenarios {#sprint-8}

The pre-Sprint-8 test surface (203 LOC / 9 cases) was honest smoke coverage but did not exercise what Sprints 4–7 built. Three rails landed:

1. **Classified unit tests** — `scripts/classify_tests.py` emits `[smoke]` / `[format]` / `[behavioral]` / `[integration]` prefixes per case. Seven new files covering FrameRing SPSC, RipplesEffect LUT, PalHeap fallback, Scheduler core affinity, PreviewModule wire format, ArtnetOutModule packet packing, StateStoreModule round-trip. Surface grew 203 → 597 LOC, 9 → 34 cases (24 behavioral, 4 integration, 2 format, 1 smoke).
2. **Runtime `[MemBoot]` / `[MemLive]` events** via `src/modules/system/MemTracker.h` (75 LOC) flow through `pmm::Logger`'s ring → serial / `/api/log` / MoonDeck log window. No file written by firmware; no `.md` status doc generated. Per-module setup brackets expose heap + PSRAM deltas with `onAllocateMemory` cost separated from `setup()` cost.
3. **Declarative scenarios** — `test/test_pc/scenarios/*.json` replayed in-process via doctest. No REST runner, no baseline file. Schema mirrors v1's so a REST runner can be unlocked later without fixture rewrite (and was — see Sprint 10).

**Bugs the tests caught while being written.** `test_frame_ring` SPSC initially asserted "no tears under any rate" — actually best-effort overwrite; pinned to paced-producer contract. `test_ripples_lut` assumed `hue_base=0` ⇒ red-dominant — wrong at 16×16 (hue rotates to cyan at corners). `test_scheduler_affinity` surfaced a use-after-free between Scheduler instances: `pal::task_create_pinned` detaches std::threads on PC; production unaffected because `run()` never returns on device.

**Tools investigation** post-sprint evaluated 11 orchestration alternatives to MoonDeck — outcome **KEEP MoonDeck**, full evaluation table moved to [backlog → Parking lot → Tools investigation](backlog.md#tools-investigation-orchestration-alternatives-to-moondeck).

## Sprint 9 — Release 1 polish {#sprint-9}

> Status: **planned, not yet executed.** Sprint 9 is the closing review pass that produces the `v1.0.0-foundation` tag.

No new features. Three workstreams: per-file minimalism review of `src/` + `test/` + guardrails (PR carries a one-line outcome per file); deploy walk via [MoonDeck](../developer-guide/deploy.md#moondeck) (every card exercised end-to-end on a fresh clone); docs read-through. Bias: removes more than it adds — net LOC ≤ 0 across `src/` + `docs/` (Sprint 8 test growth is intentional and stays). The Sprint 9 commit honours the [§3 anti-drift rule](../architecture/process.md#3-anti-drift-why-these-rules-survive) ("every release removes at least one thing").

## Sprint 10 — MoonDeck: tabbed dev console + live device surface + REST scenarios + agent loop {#sprint-10}

The script-UI grew into a real tool and earned a name. Five themes:

- **Tabbed rearchitecture** — flat card list → four panes (**PC**, **ESP32**, **Live**, **Develop**). Collapses ten per-env ESP32 cards into four tab-scoped ones with an env selector.
- **Live tab Devices list** — persistent inventory at `moondeck.json` (gitignored). Refresh probes `/api/system`; Discover sweeps a `/24` subnet via 32-thread pool; clicking a device opens its UI in the right-panel iframe.
- **REST scenario runner** — `scripts/scenario.py` (210 LOC) promoted from Sprint 8's deferred list. Replays `test/test_pc/scenarios/*.json` against `--host` or `--all-enabled` devices. Drift episode that unlocked promotion: after Sprint 8 landed in-process replay and Sprint 10 added the Devices list, there was no card that consumed the device list — the gap was visible.
- **Agent loop** — Analyze / Fix / Ask buttons below the output panel + a Develop-tab task list (**Reverse engineer sprint**, **Commit via agent**). All four endpoints stream live via SSE (POST + `fetch` + ReadableStream — `EventSource` is GET-only). User sees Claude's narration + tool calls live, same UX as a terminal run.
- **Naming** — `ui.py` → `moondeck.py`, `ui.json` → `moondeck.json`. Branded header + favicon (`docs/assets/moonlight-logo.png`). Two readings of "deck": deck of cards (the UI is literally cards) and flight deck (control). Brand-consistent with MoonModules.

Net effect on `scripts/`: `moondeck.py` 478 → 1464 LOC (+986); `scripts/scenario.py` new (+210); `scripts/test.py` -39 LOC (buffered-alignment removed, output now streams).

See [Deploy → MoonDeck](../developer-guide/deploy.md#moondeck) for the developer-facing reference.

## Sprint 11 — Docs restructure: four-section model + agent-memory framing {#sprint-11}

Sprints 1–10 grew `docs/` organically — one page per concern as it landed. By Sprint 10's close the tree had drifted into five-ish top-level entries with overlapping purposes (`adr/` next to `development/` next to `lights.md` next to `deploy.md`), and `release-01.md` had bloated to 539 lines of inline DoD checklists that nobody re-reads. This sprint reshapes `docs/` so a reader can hold its structure in one head, and adds an explicit memory-lifespan framing in CLAUDE.md so future agents read each layer with its lifespan in mind.

**Four top-level sections** — each gets its own folder with `index.md`. Replaces the previous flat list of mixed-purpose top-level files.

- **[User Guide](../user-guide/index.md)** *(new)* — per-module reference, one page per module, grouped by category (`system/` / `network/` / `lights/`). Each page splits end-user controls (table of name / type / range / default) from a developer reference (lifecycle overrides + hooks + cross-links to source). Old single-page `lights.md` deleted; eight module pages created.
- **[Architecture](../architecture/index.md)** — unchanged in scope but now has an `index.md` describing the two contracts (system + process). `system.md` updated to match what actually shipped (see below).
- **[Developer Guide](../developer-guide/index.md)** *(new)* — the "how to work in the repo" layer: [Deploy](../developer-guide/deploy.md) (renamed from top-level `deploy.md`), the [Pal inventory](../developer-guide/pal.md), and the [ADRs](../developer-guide/adr/0001-vendor-cpp-httplib.md) (moved from top-level `adr/`). Stable; updated when the way of working itself changes.
- **[Development](../development/index.md)** — what's shipped + what's next: this release file + [backlog](backlog.md). Renamed from the previous `develop/`. Churns release-by-release.

**System architecture reconciled with what shipped** (load-bearing checklist item from [Sprint 9](#sprint-9)'s docs read-through). The `MoonModule` contract code block in [system.md](../architecture/system.md#moonmodule-the-contract) had drifted — the `setup() // call addControl here` comment and the `rebuildControls()` method documented v1's pattern, not what landed in Sprint 3. Updated to the actual contract: six lifecycle virtuals + three setup-time hooks (`onBuildControls`, `onAllocateMemory`, `onUpdate`) + the control-system entry point. New "Why three setup-time hooks?" paragraph explains the runtime call order. Pal-files list reconciled with the actual files (PalRtos, PalHeap, PalUdp, PalWifi added; planned-but-not-shipped PalGpio removed; PalHeap's MALLOC_CAP_8BIT rationale captured in the inventory).

**Pal inventory promoted to its own page** ([developer-guide/pal.md](../developer-guide/pal.md), 50 LOC). The per-file inventory + LOC budgets + concerns table moved out of `system.md` (which kept the *rule*: the `#ifdef`-only-in-pal contract + the drift it guards against + the test-surface note). Plus a new module ↔ pal cross-reference table — for each module in `src/modules/**`, list which pal files it depends on, with links to each module's User Guide page. Living inventory; rule stays in architecture.

**Release 2 dissolved into backlog** ([backlog.md → Release 2 — v1 parity + cutover](backlog.md#release-2-v1-parity-cutover)). Previous `release-02.md` (66 LOC with Sprints 8–10 plans for ArtNet-in / NTP / OTA / cutover) folded into one bulleted Planned entry per sprint, plus a Release 2 deferreds line and an Open questions block (cutover path: rename or merge; Release 3 scope?). Releases aren't created until they're started — what's "next" lives in the backlog with explicit unlock conditions, not in pre-allocated release files. `release-02.md` deleted; all inbound links rewritten to the backlog anchor.

**release-01.md stripped to outcomes-only.** 539 → 134 lines (75% reduction). Each sprint is now one paragraph naming what shipped, with links to where the detail lives — module pages, pal inventory, ADRs, process architecture. The per-sprint Definition-of-Done checklists, the 60-line "Pixel-buffer sharing — design note" (moved to [RipplesEffect → Pixel-buffer sharing](../user-guide/lights/ripples-effect.md#pixel-buffer-sharing-why-a-registry-not-dynamic_cast)), and the per-sprint Deferred subsections all moved out — backlog already owned the Deferred content from the earlier consolidation. Sprint anchors (`#sprint-1` through `#sprint-10`) preserved so inbound links don't break.

**CLAUDE.md gains a "Docs as agent memory" section.** Three layers framed by lifespan:

- **Long-term memory** → `architecture/system.md` + `process.md`. The constitution. Changes only via ADR.
- **Way of working** → `developer-guide/`. Stable; how to build/flash/test and which pal each module uses.
- **Short-term memory** → `development/`. The current release + backlog. Churns release-by-release; not durable contract.

Conflict-resolution rule baked in: longer-lived layer wins. If `development/` conflicts with `architecture/`, the release doc is stale and gets fixed against architecture. This codifies the implicit rule that's been operating across Sprints 1–10 — Sprint 9's "reconcile process.md / system.md with what shipped" bullet existed precisely because release docs had drifted ahead of architecture without an ADR.

**Anchor + path sweep across the move.** Every inbound link to a moved or removed page rewritten: `develop/` → `developer-guide/` for stable artefacts (deploy, ADRs, pal), `develop/` → `development/` for in-flight artefacts (releases, backlog). Em-dash anchors (`port-and-minimize-where-substantive-modules-come-from`) tracked through several rewrites. `mkdocs build --strict` green at each step — strict mode is the only mechanical defence against this class of churn.

**Source-side incidentals.** `scripts/check_structure.py` failure message updated to point at the new `docs/developer-guide/adr/` location. `scripts/moondeck.py` JS comments tidied: removed assumptions that `release-02.md` exists; `DOCS_BASE` URLs updated from `/deploy/` to `/developer-guide/deploy/`; `scan_releases()` filesystem path moved from `docs/develop/` to `docs/development/`. `scripts/scenario.py` one stale comment fix. `mkdocs.yml` nav rewritten end-to-end.

**Minimalism stance.** Net LOC across `docs/` is *negative* — release-01.md alone shed 405 lines; release-02.md (66 LOC) and lights.md (5 LOC) deleted. New additions: 4× index.md (≤ 15 lines each), 8× user-guide module pages (~25 lines each), pal.md (50 lines), backlog.md grew by ~30 lines for Release 2 entries. The Sprint 9 "removes more than it adds" bias applied to docs.

**Source code: unchanged.** No `src/` edits. This is a docs-and-tooling sprint; the runtime ships exactly what Sprint 10 closed with.

**Why this isn't part of Sprint 9.** Sprint 9 is the *closing review* pass (per-file source/test minimalism + deploy walk + docs read-through) that produces the `v1.0.0-foundation` tag. This sprint is a docs *restructure* that emerged from doing the Sprint 9 read-through and finding the four-section model in the process. Sprint 9 still has to run end-to-end against the new structure before the tag lands.

---

## Sprint 12 — Minimalism pass: class footprint + typed controls + accurate heap accounting {#sprint-12}

> Scope: reduce `MoonModule` base size, eliminate float casts in control registration, accurately track dynamic memory, and add a static class-size checker to MoonDeck.

### Definition of Done

- [x] **`MoonModule` field reorder** (`src/core/MoonModule.h`) — fields sorted 8B→4B→2B→1B, eliminating 24 B of alignment padding. Base size 136 B → 96 B. `moduleAllocBytes_` demoted from `size_t` (8 B) to `uint32_t` (4 B); `classSize_` and `usPerTick_` demoted to `uint16_t` (2 B each). `msPerTick_` (float) replaced by `usPerTick_` (uint16_t) — same information, integer µs, no float in the base.
- [x] **`type_` as `const char*`** (`src/core/MoonModule.h`, `src/core/ModuleManager.cpp`) — `std::string type_` (24 B) replaced by `const char* type_` (8 B) pointing into the stable factory-map key. Zero heap cost; `type()` accessor no longer calls `.c_str()`. `ModuleManager::add()` stores `it->first.c_str()` (stable for the lifetime of `factories_`) or the literal `"unknown"`.
- [x] **`JsonDocument pendingProps_` → pointer** (`src/core/MoonModule.h`) — inlined 128 B slab moved to heap-allocated `JsonDocument* pendingProps_` (8 B in struct), allocated only when `setProps()`/`loadState()` is called and freed after `runSetup()` drains it. Saves 128 B per module on the common no-pending-props path.
- [x] **Typed `addControl` overloads** (`src/core/MoonModule.h`, `src/core/MoonModule.cpp`) — `uint8_t`/`uint32_t` lvalue overloads take typed `lo`/`hi` (no float cast at call sites). Four new rvalue-display overloads (`int8_t&&`, `uint8_t&&`, `uint16_t&&`, `uint32_t&&`) replace the single `float&&` catch-all. Removed one hidden footprint: `addControl(uint8_t&, …, float, float)` was silently widening integer slider ranges.
- [x] **`pal::chip_model_str()` / `pal::mac_address_str()`** (`src/pal/PalSystemInfo.h`) — function-static `const char*` variants; fill a static buffer on first call, return a stable pointer. `SystemStatusModule` removes `char chipModel_[32]` and `char macAddress_[18]` (50 B saved); `fillSystemJson()` uses the pal pointers directly.
- [x] **Pal heap functions return `uint32_t`** (`src/pal/PalSystemInfo.h`) — `total_heap_kb()`, `free_heap_kb()`, `max_alloc_kb()` changed from `float` to `uint32_t`. Heap sizes are always integer KB; float was a needless precision fiction.
- [x] **`PreviewModule` PSRAM buffer** (`src/modules/lights/PreviewModule.h`) — `std::vector<uint8_t> frame_` (24 B struct + heap alloc on regular heap) replaced by `pal::psram_alloc`-backed `uint8_t* frame_buf_` / `size_t frame_cap_`. Grows once on first frame, reused. `moduleAllocBytes_` now correctly reports the allocation. On ESP32-S3 the frame goes to PSRAM; on esp32dev it falls back to DRAM via the same code path. `teardown()` frees and zeroes.
- [x] **`RipplesEffect` ring allocation counted** (`src/modules/lights/RipplesEffect.h`) — `moduleAllocBytes_` previously omitted the `FrameRing` (2 × pixel_bytes). Added `+= 2 * pixel_bytes` after successful `ring_.allocate()`. At 75×13 this raises the reported heap from 7.9 KB to ~13.6 KB — accurate.
- [x] **`check_class_sizes.py`** (`scripts/check_class_sizes.py`) — new MoonDeck check: scans `src/` for `MoonModule` subclasses, parses fields via regex + inline-body stripping, simulates alignment, reports estimated static size per class with a per-type breakdown (pointer, std::string, float, uint32_t, …) and heap/alloc annotations. Added to MoonDeck `all-checks` card and as a standalone `check-class-sizes` card.
- [x] **`max_alloc_kb` in status bar** (`src/frontend/app.js`, `src/frontend/frontend_bundle.h`) — status bar now shows `136K free / 104K max heap` instead of just `136K free heap`. The max-alloc number surfaces fragmentation pressure that free-heap alone hides.
- [x] **RipplesEffect controls 0–255** (`src/modules/lights/RipplesEffect.h`) — `speed` and `hue_base` controls converted to 0–255 integer range for future DMX compatibility. Internal float conversion unchanged.
- [x] **Frontend µs timing** (`src/frontend/app.js`) — `ms_per_tick` → `us_per_tick` in the timing cache and display; `fmtMs()` → `fmtUs()` renders `<1000 µs` as integer µs, else as decimal ms. Integer controls no longer display fractional progress values.

### Removed

- `std::string type_` — 24 B per module, replaced by 8 B `const char*`
- `float msPerTick_` — replaced by `uint16_t usPerTick_`; same information, 6 B saved in base
- `size_t classSize_`, `size_t moduleAllocBytes_` — replaced by `uint16_t` / `uint32_t`; 10 B saved
- `char chipModel_[32]`, `char macAddress_[18]` in `SystemStatusModule` — 50 B saved; pointers into pal static buffers used instead
- `std::vector<uint8_t> frame_` in `PreviewModule` — replaced by PSRAM-backed raw pointer

### Deferred

- Progress-bar overwrite in MoonDeck Flash output (esptool `\r` lines) — attempted; esptool ANSI detection logic under a pipe proved fragile across environments. Reverted cleanly; deferred to a later sprint if the annoyance outweighs the fix cost.

---

## Sprint 13 — Shared data ring: zero-copy producer/consumer buffer infrastructure {#sprint-13}

> Scope: replace per-module pixel buffer ownership with a shared, registry-backed ring buffer. One allocation for the pixel data, consumed zero-copy by all downstream modules. Ring depth is runtime-configurable: 1 on esp32dev (no PSRAM, no cross-core), 2+ on ESP32-S3 (PSRAM, two cores). This is options B+C combined: shared ownership (C) with variable-depth ring (B).

### Motivation

Sprint 12's class-size checker exposed the allocation reality at 128×128:

| Module | Buffer | Size |
|---|---|---|
| RipplesEffect | `pixels_` (working copy) | 48 KB |
| RipplesEffect | `phase_offset_` | 32 KB |
| RipplesEffect | `base_color_` | 48 KB |
| RipplesEffect | `ring_` (2 slots) | 96 KB |
| PreviewModule | `frame_buf_` | 48 KB |
| ArtnetOutModule | UDP staging | 48 KB |
| **Total** | | **320 KB** |

On esp32dev (~180 KB internal heap, no PSRAM) a 128×128 panel is impossible. Even a 75×13 panel consumes ~40 KB of regular heap just for the ring. The root cause: every module independently allocates a full copy of the pixel data.

### Design

**`DataRing<T>`** (`src/core/DataRing.h`) — a depth-configurable SPSC ring of typed slots. Not lights-specific: any producer/consumer pair can use it. Replaces `FrameRing` which is deleted.

- `allocate(count, depth)` — allocates `depth × count × sizeof(T)` bytes via `pal::psram_alloc`. Depth 1 = single slot, no copy overhead; depth 2 = double-buffer for cross-core pipelining.
- `acquire_write_slot()` / `publish()` — producer side (same semantics as `FrameRing`).
- `try_acquire_read()` / `release_read()` — consumer side. At depth 1, returns pointer to the single slot with acquire ordering; a concurrent write is detected via revision check and the frame is skipped (acceptable at 50 fps).
- Depth 1 torn-frame contract: producer bumps revision before write (relaxed), consumer reads revision before and after (acquire/acquire); if they differ, skips the frame.

**`DataRegistry`** (`src/core/DataRegistry.h`) — replaces `PixelRegistry`. Maps string id → `DataRing<RGB>*` + geometry metadata (width, height, depth). Lives in core (the geometry is T-agnostic metadata; RGB is only in the leaf modules that use it).

- `declare(id, count, ring_depth)` — called by the producer in `onAllocateMemory`. Creates or reallocates the ring. Ring depth sourced from `pal::psram_size() > 0 ? 2 : 1` by default, overridable via control.
- `resolve(id)` → `DataRing<RGB>*` — called by consumers in `setup()`. Returns null if not yet declared (tolerate late producers, same pattern as today).
- `undeclare(id)` — called by producer in `teardown()`. Frees the ring; consumers get null on next `try_acquire_read`.

**`PixelSource` / `PixelBufferRef`** (`src/modules/lights/Pixelable.h`) — kept as the lights-domain consumer interface but backed by `DataRing<RGB>` instead of `FrameRing`. `pixelBuffer()` returns a `PixelBufferRef` wrapping a `DataRing` slot pointer + geometry.

**RipplesEffect** — removes `FrameRing ring_` (32 B struct, 96 KB heap at 128×128). Calls `DataRegistry::declare` in `onAllocateMemory`; writes directly into the ring slot in `loop20ms`. `pixels_` working buffer kept (48 KB) — the effect still needs to accumulate the frame before publishing.

**PreviewModule** — removes `frame_buf_` entirely (48 KB at 128×128). Reads the ring slot directly via `DataRegistry::resolve` and packs the wire format on-the-fly into `ws_->broadcastBinary` without a staging buffer. Net: zero allocation in PreviewModule.

**ArtnetOutModule** — reads ring slot directly; UDP staging buffer unchanged (needed for protocol framing).

### Memory at 128×128 after Sprint 13

| Module | Buffer | Size |
|---|---|---|
| RipplesEffect | `pixels_` (working copy) | 48 KB |
| RipplesEffect | `phase_offset_` | 32 KB |
| RipplesEffect | `base_color_` | 48 KB |
| DataRegistry | ring slot(s) — depth 1 (esp32dev) | 48 KB |
| DataRegistry | ring slot(s) — depth 2 (esp32s3) | 96 KB |
| PreviewModule | *(none)* | 0 KB |
| ArtnetOutModule | UDP staging | 48 KB |
| **Total esp32dev** | | **176 KB** (-144 KB) |
| **Total esp32s3** | | **224 KB** (-96 KB) |

### Definition of Done

- [ ] **`DataRing<T>`** (`src/core/DataRing.h`) — templated depth-configurable SPSC ring; replaces `FrameRing`. Depth-1 torn-frame detection via before/after revision compare. `allocate(count, depth)` uses `pal::psram_alloc`. Passes unit tests in `test/test_pc/`.
- [ ] **`DataRegistry`** (`src/core/DataRegistry.h`) — string-keyed registry of `DataRing<RGB>*` + geometry. `declare` / `resolve` / `undeclare`. Singleton (same pattern as `PixelRegistry`).
- [ ] **`FrameRing` deleted** (`src/modules/lights/FrameRing.h` removed) — replaced entirely by `DataRing`. No lights-domain type in core.
- [ ] **`PixelRegistry` deleted** (`src/modules/lights/PixelRegistry.h` removed) — replaced by `DataRegistry` in core.
- [ ] **`RipplesEffect` updated** — removes `FrameRing ring_` field; calls `DataRegistry::declare` in `onAllocateMemory`; writes ring slot in `loop20ms`. `moduleAllocBytes_` reports working buffers only (ring owned by registry).
- [ ] **`PreviewModule` updated** — removes `frame_buf_` / `frame_cap_`; resolves `DataRing<RGB>` from `DataRegistry`; packs wire format directly from ring slot pointer in `loop20ms`. Zero allocation.
- [ ] **`ArtnetOutModule` updated** — resolves `DataRing<RGB>` from `DataRegistry` instead of `PixelSource`.
- [ ] **`check_class_sizes.py` scenarios updated** — `RipplesEffect` scenario removes ring from per-module count; adds a `DataRegistry` line showing ring cost at each panel size and depth.
- [ ] **Build green** on `pc`, `esp32dev`, `esp32s3_n16r8`.

### Removed

- `src/modules/lights/FrameRing.h` — lights-domain SPSC ring; replaced by generic `DataRing<T>` in core
- `src/modules/lights/PixelRegistry.h` — lights-domain registry; replaced by `DataRegistry` in core
- `frame_buf_` / `frame_cap_` in `PreviewModule` — zero-copy read from shared ring eliminates staging buffer
- `ring_` field in `RipplesEffect` — ring now owned by `DataRegistry`, not the effect module

### ADR required

`DataRing` and `DataRegistry` move into `src/core/` — this crosses the core boundary as defined in [architecture/system.md](../architecture/system.md) (core currently contains only `Module`, `ModuleManager`, `Scheduler`, `Pal`). The justification: `DataRing` is a concurrency primitive (SPSC ring with acquire/release semantics), not a domain type — it belongs alongside `Scheduler` as core infrastructure. `DataRegistry` is a typed singleton store, analogous to `ModuleManager`. An ADR will be filed before implementation.

## Sprint 14 — Ring → single-slot buffer; patch convention; script budgets; frontend.md; doc crosslink pass {#sprint-14}

Two independent workstreams landed together.

**DataRing → DataBuffer.** Sprint 13's `DataRing<T>` carried a `depth` parameter (1 on esp32dev, 2 on S3). The depth was identified as overdesign: a ring with depth > 1 *within one module* puts the double-buffer boundary in the wrong place — it belongs between two separate modules (producer owns one slot, consumer owns its own). `DataRing.h` was deleted and replaced by `DataBuffer.h`: a single pre-allocated slot with the same acquire/release atomics and revision counter, but no depth concept. `test_frame_ring.cpp` was deleted; `test_data_buffer.cpp` replaced it with seven focused cases. `RipplesEffect`, `ArtnetOutModule`, and `PreviewModule` updated; `DataRegistry` entry renamed `DataBufferEntry` (field `ring_ptr` → `buf_ptr`, `depth` removed). ADR 0003 updated to reflect what actually landed.

**PATCH: comment convention + script LOC budgets.** Workarounds that exist because of a missing backend feature are now annotated `// PATCH: <name>` (C++) or `# PATCH: <name>` (Python) at the call site. `scripts/check_patches.py` scans `src/` and `scripts/` for these markers and lists them with their removal conditions (informational, exit 0). Four patches annotated in source: `drag-guard` and `schema-diff` in `app.js`, `queue-headroom` in `PalWs.h`, `wifi-guard + WiFiUDP` in `PalUdp.h`. `moondeck-monolith` annotated in `moondeck.py` and a `check-patches` card added to MoonDeck. `scripts/check_loc.py` extended with a `SCRIPT_BUDGETS` dict covering all `scripts/*.py` files — new scripts must have a budget entry or CI fails. MoonDeck header made sticky (`position: sticky; top: 0`). `BrokenPipeError` after flash fixed in `_send_json()`. Ruff F841 unused variable in `check_ui.py` removed. Frontend bundle regenerated.

**Docs.** `frontend.md` added (full `app.js` section-by-section walkthrough, `style.css` structure, architectural fit). `backend.md` added (DataBuffer API, DataRegistry usage pattern, layering design, module lifecycle sequence). Both added to mkdocs.yml nav. Layering section moved from `backend.md` to `architecture/system.md` (architectural concept); `backend.md` keeps the `map_blend` implementation detail. Crosslink pass across `pal.md`, `frontend.md`, `process.md`, `deploy.md`, `user-guide/lights/`, ADR 0003, and `backlog.md`: stale `FrameRing`/`PixelRegistry`/`PixelSource` references replaced, LOC budget table in `deploy.md` updated to match `check_loc.py`.

### Removed

- `src/core/DataRing.h` — depth-configurable SPSC ring; ring concept is the wrong boundary; replaced by `DataBuffer.h` (single slot)
- `test/test_pc/test_frame_ring.cpp` — ring-specific tests; replaced by `test_data_buffer.cpp`
- `depth` field from `DataBufferEntry` / `DataRegistry` — no longer a per-buffer concept
- `default_depth()` static method from `DataRegistry` — concept removed with depth
- Unused `has_pointerdown` variable in `check_ui.py` (ruff F841)

### Definition of Done

- [x] `src/core/DataBuffer.h` — single-slot SPSC; `allocate(count)`, `acquire_write`, `publish`, `try_acquire_read`, `release_read`, `revision`. All 36 PC tests green.
- [x] `src/core/DataRing.h` deleted; `src/core/DataRegistry.h` updated (`DataBufferEntry`, `buf_ptr`, no `depth`).
- [x] `RipplesEffect`, `ArtnetOutModule`, `PreviewModule` updated to `DataBuffer<RGB>`.
- [x] `test/test_pc/test_data_buffer.cpp` — 7 cases covering allocation, publish/read, revision, SPSC no-tear.
- [x] `scripts/check_patches.py` — scans `src/` + `scripts/` for `// PATCH:` / `# PATCH:` markers; informational.
- [x] Four `PATCH:` annotations in source; `check-patches` card in MoonDeck.
- [x] `SCRIPT_BUDGETS` in `check_loc.py` covers all `scripts/*.py`; new scripts without a budget entry fail CI.
- [x] `docs/developer-guide/frontend.md` — full frontend walkthrough; added to mkdocs.yml.
- [x] `docs/developer-guide/backend.md` — DataBuffer/DataRegistry API + usage pattern + layering + lifecycle; added to mkdocs.yml.
- [x] Layering section in `system.md` (concept); `map_blend` detail in `backend.md`.
- [x] Crosslink pass complete: no stale `DataRing`/`FrameRing`/`PixelRegistry`/`PixelSource` in docs outside historical sprint entries.
- [x] ADR 0003 updated to reflect `DataBuffer<T>` and the ring-depth removal rationale.
- [x] Build green on `pc`. ESP32 build not re-verified this sprint (no hardware change beyond renaming; structural equivalence confirmed by test suite).

---

## Sprint 15 — GridLayoutModule: geometry flow + serpentine wiring {#sprint-15}

First step of the light domain architecture. Effects stop hard-coding geometry; a `GridLayoutModule` becomes the single authority on panel dimensions and physical wiring.

**Phase 1 — geometry flow.**

`RipplesEffect` currently allocates `w·h·d` from its own width/height/depth controls. After this sprint those controls are removed; the effect receives its virtual dimensions from whatever is linked to it. Two paths:

- **With a `GridLayoutModule` linked**: the effect resolves the layout module by ID (a `layout` control, text field), calls `physical_count()` to size its buffer, and calls `width()` / `height()` / `depth()` to know its virtual coordinate space.
- **With no layout linked** (default / fallback): the effect defaults to 16×16×1 and allocates accordingly.

The effect links directly to a `GridLayoutModule` by ID — no `EffectLayer` parent is required yet. This keeps the first-effect path as simple as possible; `EffectLayer` grouping comes in a later sprint.

`GridLayoutModule` exposes:
- `width`, `height`, `depth` — slider/number controls (panel bounding box)
- `physical_count()` — explicit pixel count (for Phase 1: `width × height`; sparse layouts come later)
- `map(logical_idx) → physical_idx` — serpentine / zigzag remapping (controlled by a `serpentine` toggle)

`PreviewModule` and `ArtnetOutModule` continue to resolve `RipplesEffect`'s `DataBuffer<RGB>` directly by ID. Because the effect's buffer is now sized from the layout, they automatically get the correctly-sized frame — no source change required.

**Phase 2 — UI.**

The frontend already renders all controls from schema. Phase 2 verifies the UI shows the full wiring correctly:
- `GridLayoutModule` card with `width`, `height`, `serpentine` controls visible and editable.
- `RipplesEffect` card shows the `layout` source control (text field pointing at the layout module's ID).
- Changing `width` or `height` in the UI triggers `onUpdate` → `onAllocateMemory` on `RipplesEffect` → Preview and ArtNet display the new geometry immediately.
- No layout linked: effect defaults to 16×16 and UI reflects that.

### Definition of Done

- [x] `GridLayoutModule` (`modules/lights/GridLayoutModule.h`) — `width`, `height`, `depth`, `serpentine` controls; implements `physical_count()` and `map(logical_idx)`; resolved by consumers via direct manager lookup.
- [x] `RipplesEffect` geometry controls (`width`, `height`, `depth`) removed; replaced by a `layout` source control (text, ID of a linked `GridLayoutModule`). Fallback: 16×16×1 when no layout linked.
- [x] `RipplesEffect.onUpdate("layout")` resolves the linked `GridLayoutModule` and triggers `allocate_()` to resize.
- [x] `RipplesEffect.loop1s()` polls layout dimensions; calls `allocate_()` on geometry mismatch — live resize without reboot.
- [x] `RipplesEffect` buffer sized from `GridLayoutModule::physical_count()` (or 16×16×1 fallback).
- [x] `PreviewModule.loop1s()` polls `DataRegistry` for geometry change; calls `resolve_buf_()` on mismatch — preview updates live.
- [x] `PreviewModule` and `ArtnetOutModule` unchanged interface — still resolve `RipplesEffect` buffer by ID; frame header reflects new geometry automatically.
- [x] `CtrlType::Uint16` added; `addControl(uint16_t& v, …)` overload wired through all 6 switch sites — `width_`, `height_`, `depth_` register as mutable sliders, not read-only FloatConst.
- [x] PC build green; 37 tests pass.
- [x] UI: `GridLayoutModule` card visible; `width`/`height`/`depth`/`serpentine` editable; `RipplesEffect` card shows `layout` control; geometry change in UI resizes preview and ArtNet output live (HIL verified on esp32dev).
- [x] `backlog.md` Step 1 unlock condition updated.

### Additional fixes landed with Sprint 15

- **`PalWs.h` race fix** — `broadcastText`/`broadcastBinary` switched from manual `getClients()` iteration (no lock) to `ws_.textAll()`/`ws_.binaryAll()` which acquire `_ws_clients_lock` internally. Eliminated a `LoadProhibited` crash when the async_tcp task on core 0 cleaned up a disconnecting client while core 1 was iterating the client list in `broadcastBinary`.
- **`StateStoreModule` lost-module fix** — `/modules.json` write moved from `loop10s` to `loop1s`. A crash within 10 s of adding a module no longer loses the module list. Per-module state files remain on `loop10s` (larger, change frequently).
- **`WebSocketModule` static output buffers** — `broadcast_schema_()` and `broadcast_state_()` serialize into `static char buf[]` (4 KB + 2 KB in `.bss`) instead of heap-allocated `std::string`. Eliminates one heap allocation per 1 Hz broadcast cycle.
- **OOM crash logged to backlog** — remaining crash path (ArduinoJson `JsonDocument` DOM heap allocation under extreme memory pressure) recorded in `backlog.md` with root cause, investigated approaches, and unlock conditions.

---

## Sprint 16 — Module tree drag & reparent {#sprint-16}

Second step of the light domain architecture (UI layer). The module tree becomes a live, drag-reorderable graph: modules can be moved between parents, promoted to root, or demoted as children entirely from the browser. Reordering changes loop execution order immediately. The data model and REST protocol gain a `group` slot so future container modules (EffectGroup, DriverGroup, or any domain-specific grouping) can land without a protocol break.

### Design context — nodes and noodles

The module tree is a **tree-shaped DAG**: the structural parent/child tree gives one parent per module (grouping, loop order); string-ID controls on modules act as the data-flow edges ("noodles"), allowing arbitrary fan-in from anywhere in the graph. This is the same model as TouchDesigner — tree hierarchy for organization, wires for data flow — with the wires stored as control values rather than explicit edge objects (a valid tradeoff for an embedded target).

The design is an established pattern. Three guardrails follow from the nodes-and-noodles survey:

- **String-ID validation at setup time** — every module resolves and validates its string-ID inputs in `setup()` / `onUpdate()`; missing or wrong-type references log clearly and fail safely.
- **Connections surfaced in the UI** — each module card's controls section shows its string-ID inputs (e.g. `layout → layout-0`) so the implicit data-flow edges are visible without a full canvas editor.
- **`is_group` is domain-neutral** — a group node is a structural container; the light-domain semantics (EffectLayer, DriverLayer) are attributes of the module type, not of the grouping mechanism.

### What exists (no work needed)

- `MoonModule::addChild` / `removeChild` / `reorderChildren` — child management in backend
- `buildTree(modules)` in `app.js` — builds two-level tree from `parent_id` field in schema JSON
- Drag-to-reorder within root nav (`saveNavOrder` → `POST /api/modules/reorder` with `parent_id: ""`)
- Drag-to-reorder within a parent's children (`saveChildOrder` → same endpoint, `parent_id` set)
- `POST /api/modules/reorder` endpoint — exists in `HttpServerModule`; nested `parent_id` path is stubbed with a comment

---

### Step 1 — Backend reparent + reorder (C++ only, no UI change)

All backend plumbing. Frontend untouched. Done signal: tests pass, `check_loc` green, HIL confirms reparent survives reboot.

**1a. `parent_id` field in schema JSON** — every module's schema object includes `"parent_id": "<id or empty>"`. `MoonModule` gains a `parent_` back-reference (raw pointer, set by `addChild` / `removeChild`); serialised as `parent_id` in `getSchema`. This is the source of truth for `buildTree` in the frontend.

**1b. `POST /api/modules/reparent`** — new endpoint in `HttpServerModule`. Body: `{ "id": "ripples-0", "parent_id": "layout-0" }`. Steps:
- Remove module from its current parent's children list (or from the root order).
- Add as last child of the new parent (or append to root list if `parent_id` is empty).
- Re-sort `modules_` flat vector to depth-first tree order so loop execution order matches the visual tree (parent always before its children).
- Trigger auto-wire (1c).
- Mark schema dirty; next WS push reflects the change.

**1c. Auto-wire on reparent** — after a reparent, iterate the moved module's controls. For every `CtrlType::String` control whose key matches the new parent's type name (e.g. key `"layout"`, parent type `"layout"`), call `setControl(key, new_parent->id())`. All matching controls are wired, not just the first. On detach (`parent_id: ""`), reset matching controls to `""`.

**1d. `POST /api/modules/reorder` — nested path** — the existing stub comment (`"reserved for nested reorder"`) replaced with a real implementation: when `parent_id` is non-empty, call `reorderChildren` on the named module and re-sort `modules_` to maintain depth-first order. Root path unchanged.

**1e. `is_group` flag in schema JSON** — `virtual bool isGroup() const { return false; }` on `MoonModule`; emitted as `"is_group": false` in every schema object. A group module is a full `MoonModule` — controls, hot-path loops, memory allocation — distinguished only by this flag, which tells the frontend to render it as a container (own controls collapsible above children) rather than a leaf card. "Layer" intentionally avoided: that term implies compositing semantics from Blender/Photoshop/TouchDesigner which don't belong at the protocol level.

**1f. `StateStoreModule` persistence** — `parent_id` added to `/modules.json` per module entry; restored on boot so the full tree topology survives reboot.

**1g. `test_reparent.cpp`** — new test file covering:
- Reparent child to new parent → `parent_id` set; auto-wire fires; matching string controls updated.
- Reparent to root → `parent_id` cleared; matching controls reset to `""`.
- Loop order after reparent: parent precedes child in `modules_` traversal.
- Nested reorder: `reorderChildren` + depth-first re-sort produce the expected order.

#### Step 1 Definition of Done

- [ ] `MoonModule` carries `parent_` back-reference; set by `addChild` / `removeChild`; serialised as `parent_id` in schema JSON.
- [ ] `POST /api/modules/reparent` — updates children arrays, re-sorts `modules_`, triggers auto-wire, marks schema dirty.
- [ ] `POST /api/modules/reorder` — nested path (`parent_id` non-empty) calls `reorderChildren` and re-sorts `modules_`; root path unchanged.
- [ ] Auto-wire fires on reparent for all matching string controls; clears on detach.
- [ ] `modules_` flat vector always in depth-first tree order after any reparent or reorder.
- [ ] `virtual bool isGroup() const { return false; }` on `MoonModule`; `"is_group"` emitted in schema JSON.
- [ ] `StateStoreModule` persists and restores `parent_id` in `/modules.json`.
- [ ] `test_reparent.cpp` passes; budget entry added to `check_loc.py`.
- [ ] `check_loc` green.
- [ ] HIL: `POST /api/modules/reparent` via REST → module moves; reboot → tree topology restored.

---

### Step 2 — Tree view drag-reparent (frontend, tree UI only)

Exercises the Step 1 backend from the browser. The tree view gains cross-level drag-drop. Canvas view not yet present. Done signal: HIL drag ripples onto layout → `layout` control auto-fills; drag back → clears; group modules render as containers.

**2a. Cross-level drag: root → child** — dragging a root nav item and dropping it onto another root module's content area calls `POST /api/modules/reparent` with the drop target's id as `parent_id`. The nav item disappears from root; the module appears as the last child of the target.

**2b. Cross-level drag: child → root** — dragging a child module's handle out of the parent card and dropping it onto the root nav calls `POST /api/modules/reparent` with `parent_id: ""`. The module is promoted to root.

**2c. Visual drop zones** — while dragging:
- Root nav items show a distinct "reparent here" indicator when a nav item hovers over them (separate from the sibling-reorder indicator).
- A drop zone strip appears at the bottom of the root nav when a child handle enters the nav region.
- Sibling reorder within a parent unchanged.

**2d. Group module rendering** — modules with `is_group: true` in schema rendered as container nodes: own controls (if any) in a collapsible header above the children list, not as a standalone leaf card.

**2e. String-ID connections surfaced** — each module card lists its string-ID controls in a "Connections" subsection (e.g. `layout → layout-0`) so data-flow edges are visible in the tree view without a canvas.

#### Step 2 Definition of Done

- [ ] Cross-level drag root → child calls `reparent`; nav item moves to children in tree.
- [ ] Cross-level drag child → root calls `reparent` with `parent_id: ""`; module promoted to root nav.
- [ ] Visual drop zones distinguish reparent target from sibling reorder target.
- [ ] Group modules (`is_group: true`) rendered as containers with collapsible controls header.
- [ ] String-ID controls shown in a "Connections" subsection on each module card.
- [ ] HIL: drag ripples onto layout → `layout` control auto-fills with `layout-0`; drag back to root → control clears.

---

### Step 3 — Canvas view PoC + toggle (additive, no regressions)

Purely additive. Reads the same module list produced by Steps 1 and 2. Touches no existing code paths. If this step slips, Steps 1 and 2 ship independently.

**3a. Tree/canvas toggle** — a button in the header switches between `#tree-view` (existing, default) and `#canvas-view` (new). Both read from the same in-memory module list; state is shared.

**3b. Canvas renderer (`renderCanvas()`)** — self-contained function (~150–200 lines of JS, no new dependencies):
- One `<div id="canvas-viewport">` with `overflow: hidden`; one inner `<div id="canvas-world">` transformed for pan/zoom.
- Per-module `<div>` boxes, `position: absolute`, placed at `{x, y}` from `localStorage` (`canvas_x_<id>`, `canvas_y_<id>`); first-open defaults to a simple grid arrangement.
- `mousedown` / `mousemove` / `mouseup` on a box drags it; position written back to `localStorage` on drop. No backend call — positions are a UI concern only in the PoC.
- `wheel` on viewport zooms; drag on the background pans.

**3c. Noodles (SVG overlay)** — one `<svg>` layer covering the canvas world:
- One cubic bezier path per string-ID control: source = right edge of the referenced module's box; destination = left edge of the control owner's box.
- Structural parent edges: solid, 2 px.
- Data-flow noodles: dashed, 1.5 px, coloured by control type (one colour per key name, derived from a small palette).
- Noodles are read-only in the PoC — they reflect existing connections; drawing new noodles is deferred.
- Noodles update on box drag and on each schema push.

**3d. Sidebar on click** — clicking a module box selects it; the existing `buildCard(mod)` output is injected into a `<div id="canvas-sidebar">` beside the canvas. No controls rendering is duplicated.

**3e. Canvas position persistence** — `canvas_x_<id>` / `canvas_y_<id>` keys in `localStorage`. Noted in code with a comment pointing to future backend promotion if the canvas becomes the primary UI.

#### Step 3 Definition of Done

- [ ] Toggle button switches tree ↔ canvas; both views stay in sync with the module list.
- [ ] Module boxes draggable; positions persist in `localStorage`; first-open shows grid layout.
- [ ] Noodles: solid lines for parent edges, dashed for data-flow; update on drag and schema push.
- [ ] Clicking a box opens the module's controls card in the sidebar.
- [ ] Pan (drag background) and zoom (wheel) work on the canvas viewport.
- [ ] Tree view unchanged when canvas view is active; toggling back restores tree state.
- [ ] HIL: toggle to canvas view → modules as boxes with noodles; pan, zoom, drag work; sidebar opens on click; toggle back → tree unchanged.

---

## Sprint 17 — The parent *is* an input (core unification) {#sprint-17}

Sprint 16 shipped reparent with two states for one intent: a structural `parent_` pointer **and** the data-flow string controls, kept in sync by `auto_wire_()` (a name-match copy heuristic). The canvas made the redundancy visible — a module nested under a parent *and* showing a noodle to the same parent. The "sometimes a source is not linked right" bug is a direct symptom: two states, copied by a heuristic, drift.

This sprint removes the duplication. **The parent is not a separate concept — it is an input that carries a parent flag.** This is a core architecture change; it is recorded in [architecture/system.md](../architecture/system.md) (§ Inputs, and the parent input), not an ADR — making the model *more* minimal is the system architecture's own job, per [Rule #1](https://github.com/ewowi/projectMM-v2/blob/main/CLAUDE.md). This sprint brings the code into line with that page.

### What changes conceptually

| Sprint 16 (removed) | Sprint 17 (the model) |
|---|---|
| `MoonModule::parent_` raw pointer | parent flag on a `ControlDescriptor` |
| `auto_wire_()` copies parent id into a name-matched control | reparent *sets the flag on* the name-matched input — nothing copied |
| reparent always succeeds (pointer is universal) | reparent rejected if no input name matches the parent's type |
| detach clears the matched control value | promote clears the *flag*, keeps the value (link survives as data-flow) |
| `is_group` virtual | a "group" is any module that has children via a parent-flagged input — flag removable from schema once tree/canvas read children from the relationship |

Loop semantics are **identical**: the parent-flagged input drives `sort_depth_first_()` exactly as `parent_` did. Pure refactor of the lookup; runtime behavior unchanged. The backlog `runChildren()` / child-dispatch-timing item stays deferred — explicitly out of scope here.

### Step 1 — Core: parent flag replaces `parent_` (C++ only)

- **1a.** Add a `bool isParent` (or a reserved sentinel) to `ControlDescriptor`. At most one input per module carries it.
- **1b.** `MoonModule::parent()` resolves via the flagged input's value (`manager_->find(flaggedInput.value)`) instead of a stored pointer. `childCount()` / `child(i)` derive from "modules whose parent-flagged input points at me." Remove `parent_`.
- **1c.** `ModuleManager::reparent(id, parentId)`: find the child's input whose **name == parent's type** (`strcmp`); if none, **return false** (drop rejected). Otherwise set that input's value to `parentId` and raise its parent flag. Delete `auto_wire_()` — its job is now the operation itself, not a follow-up copy.
- **1d.** Promote to root (`parentId == ""`): clear the parent flag on whatever input held it; **keep the value**. Module becomes a root; the input is a normal visible input again.
- **1e.** `sort_depth_first_()` walks the parent relationship via the flagged-input lookup. Traversal order unchanged.
- **1f.** System modules declare a `system` input so they can be nested under a system module by the same rule. No implicit/universal parent input.
- **1g.** `StateStoreModule`: persist which input is parent-flagged (the value is already persisted as a control). Restore order unchanged (two-pass: add all, then re-apply flags).
- **1h.** `test_reparent.cpp` updated: reparent by name match; reject when no matching input; promote keeps value clears flag; loop order unchanged; round-trip through StateStore.

### Step 2 — Frontend: one relationship, no doubling

- **2a.** `buildTree` reads the parent from the parent-flagged input (schema exposes which input is flagged), not a separate `parent_id`.
- **2b.** Canvas: a parent-flagged input renders as **nesting only** — suppress its noodle (no more nested-box + duplicate noodle). An unflagged id input renders as a noodle only. Exactly the duality made visible without redundancy.
- **2c.** Drag-drop reparent: if the backend rejects (no name match), the UI shows a no-drop indicator and the module stays put.

### Step 3 — Module resolution precedence (lights domain)

- **3a.** `RipplesEffect` (and any effect with overlapping geometry sources) resolves geometry from a fixed local precedence: parent `layer`'s resolved layout → own `layout` input → 16×16 default. Evaluated in `onUpdate`/`onAllocateMemory`, never the hot loop. This logic stays in the module; core gains nothing.

#### Sprint 17 Definition of Done

- [x] Single source of truth is the parent-flagged input (`parentControlIdx_`). `parent_`/`children_[]` are kept as a **derived index** rebuilt by targeted relink in `reparent` — *not* removed: deleting them would add O(n) scans into the hot loop recursion and re-introduce a cache, for worse perf and more code. The DoD originally said "removed"; the validated design keeps them derived (zero hot-path cost). `system.md` reflects this.
- [x] `auto_wire_()` deleted; reparent sets the flag on the matched input directly.
- [x] Match rule: input name == parent type, **or** input name is the conventional wildcard `source` (precise match wins over `source`). No name match → reparent rejected (returns false, UI no-drop).
- [x] Promote to root clears the parent flag but preserves the input value (link degrades to data-flow).
- [x] `sort_depth_first_()` loop order identical to Sprint 16 (test asserts unchanged traversal).
- [~] System-module `system` input — **deferred to [backlog](backlog.md)**: no current need to nest system modules; adding a speculative input to every system module with no consumer violates the minimalism rule. No implicit universal input exists (a module with no matching input simply cannot be a child).
- [x] Canvas: parent-flagged input shows as nesting only (no duplicate noodle); unflagged id input shows as noodle only. Parent-flagged input also hidden in the tree card (one model, two views).
- [x] `RipplesEffect` geometry precedence: parent chain → own `layout` input → 16×16, all cold-path.
- [x] `StateStoreModule` round-trips via the persisted input value; pass-2 `reparent` re-derives the flag. HIL-verified: reboot restores the full nested tree.
- [x] `system.md` § "Inputs, and the parent input" matches the shipped code (incl. wildcard rule); `check_loc` green; `test_reparent.cpp` passes (45/45, incl. wildcard + exact-wins cases).
- [x] HIL: ripples→layout nests, no duplicate noodle; preview/artnet→ripples nests via `source` wildcard; incompatible drop rejected; promote ripples → root, `layout` input retained, noodle appears; reboot → tree restored.
