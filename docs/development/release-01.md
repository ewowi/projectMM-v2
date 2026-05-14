# Release 1 — Restart to Parity

Bring v2 to parity with v1's first-boot pipeline — effect → blend → driver → preview, served over HTTP / WS with WiFi-STA, persisted to LittleFS — implemented as modules over a small core. The decision to restart from v1 is documented in v1's [Release 9](https://ewowi.github.io/projectMM/development/release-09/). The contract under which this release executes is [process architecture](../architecture/process.md): minimalism, guardrails, anti-drift. Items deferred from any sprint live in [backlog.md](backlog.md).

## What Release 1 delivers

A v2 codebase that runs v1's first-boot pipeline as modules over a small core: [`MoonModule`](../architecture/system.md#moonmodule-the-contract) + [`ModuleManager`](../architecture/system.md#modulemanager-instance-ownership) + [`Scheduler`](../architecture/system.md#scheduler-dag-runner-across-cores) + a minimal [`Pal`](../developer-guide/pal.md). Networking, persistence, the HTTP / WS server, and the entire lighting domain are modules. The deploy surface is [MoonDeck](../developer-guide/deploy.md#moondeck) + a handful of scripts.

CI-enforced minimalism budgets are the load-bearing constraint: core ≤ 300 LOC, pal files capped individually ([pal inventory](../developer-guide/pal.md)), per-module 200–300 LOC depending on domain. Overshoot fails CI; bumps require an explicit signed-off edit to `scripts/check_loc.py`. Concurrency is [arbitrary DAG, SPSC per edge, depth 2 by default](../architecture/system.md#concurrency-model).

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

The v1 → v2 cutover (rename + final stable tag) closes [Release 2](backlog.md#release-2-v1-parity-cutover), which adds ArtNet **in**, OTA, NTP, and any remaining v1 parity bits.

---

## Sprint 1 — Guardrails and skeleton {#sprint-1}

The minimum guardrails framework that the empty `Module` / `Manager` / `Scheduler` / `Pal` skeleton justifies — no more. Four lifecycle cadences (`loop`, `loop20ms`, `loop1s`, `loop10s`) as first-class scheduler concerns from commit 1, not afterthoughts. Linux-PC CI green; macOS / Windows / ESP32 envs land when those platforms gain real code. Pre-commit hook + CI gates active for raw-GPIO ban, hot-path allocation ban, hot-path blocking-call ban, structural-additions allowlist, LOC budget. Per-script `moondeck.py` cards from day one — see [Deploy → MoonDeck](../developer-guide/deploy.md#moondeck).

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
- **REST scenario runner** — `scripts/scenario.py` (210 LOC) promoted from Sprint 8's deferred list. Replays `test/test_pc/scenarios/*.json` against `--host` or `--all-enabled` devices. Drift episode that unlocked promotion: device list with no live runner to consume it — see [Sprint 8 § Tools investigation](#sprint-8) and the [Artefact promotions](#artefact-promotions) ledger.
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

---

## Artefact promotions {#artefact-promotions}

Each line records the promotion of a deferred test or tooling artefact back into an active sprint. Format: `YYYY-MM-DD — promoted <artefact>. Drift episode: <one-line description>.` Without a drift-episode entry, no promotion lands. This is the [§3 anti-drift rule](../architecture/process.md#3-anti-drift-why-these-rules-survive) applied to test infrastructure itself.

- **2026-05-13** — promoted **REST scenario runner** (`scripts/scenario.py`). Drift episode: after Sprint 8 Rail 3 landed in-process replay and Sprint 10's Live tab added a discovered-Devices list, there was no card that *used* the device list — the "Run scenarios" card still only exercised the maintainer's PC via `pio test`. The gap was visible: a device list with no live runner to consume it.

---

## Validated during Release 1

Items from v1's Release 9 guardrails outline that had to earn their place under the minimalism rule.

- **Tool choices for the three guardrail tiers** — **REPLACED.** None of v1's candidates (`clang-format`, `ruff`, `clang-tidy`, `cppcheck`) adopted. Each tier ships a purpose-built Python check in `scripts/check_*.py`. No formatter was added — mechanical formatting wasn't on v1's failure list.
- **Hot-path enforcement mechanism** — **KEPT (regex/Python).** `scripts/check_hot_path.py` matches `void [Class::]loop*() {` with brace-balanced body extraction and a banned-pattern scan. ~50 LOC. The clang-tidy plugin and AST options were not adopted — overkill for the surface.
- **Footprint baseline format** — **REPLACED.** Budgets live inline in `scripts/check_loc.py`'s `BUDGETS = {...}` dict. Bumps are PR-visible inline edits. No separate `baselines/footprint.json` was created.
- **Structural-additions justification format** — **REPLACED.** Top-of-file docstring (Python) or `//` block (C++). Top-level directory additions require an ADR — enforced by `scripts/check_structure.py`'s allowlist.
- **Verifier-of-the-verifier** — **REPLACED.** The "growth gated by the structural rule" half stayed (`check_structure.py`). The `healthReport()` meta-test never landed and isn't needed — the test surface stays readable in one sitting; meta-assertion buys nothing at that size.

DROPPED outcomes (doc-growth budget number, `test_techdebt.cpp`-style encoded TODO tests, `test_health_checks.cpp` meta-test) are recorded in [backlog.md → Parking lot](backlog.md#parking-lot).
