# Backlog

Project-wide register of work that has been considered but is not landing in the current release. Per-release sprint plans link here instead of carrying their own Deferred sections inline; that keeps each release doc focused on what *did* happen.

Two sections by purpose:

- **[Planned for next releases](#planned-for-next-releases)** — work that has a clear path back. Each entry names its origin sprint and the **unlock condition** — the drift episode or scope trigger that would promote it back to an active sprint. Without an unlock condition, the entry does not belong here.
- **[Parking lot](#parking-lot)** — investigations done and outcomes recorded so the same evaluation doesn't get re-run. Each entry names what was rejected (or dropped) and why. Entries here are not expected to come back; they exist to prevent re-litigation.

The boundary: if it has an unlock condition with a realistic trigger, it's Planned. If it's a recorded "we looked, we declined", it's Parking lot.

---

## Planned for next releases

### Release 2 — v1 parity + cutover

Release 2 closes the v1 → v2 transition: the v1 features that didn't make Release 1, plus the rename of this repo from `projectMM-v2` to `projectMM` with the first stable `v2.0.0` tag. Order is deliberate — ArtNet-in lets the device be driven from real DMX consoles before OTA changes how firmware gets onto it; NTP comes free with the WiFi stack already running; OTA needs filesystem persistence + WiFi (both ready since Release 1). Per-module budget ≤ 300 LOC.

- **ArtNet-in module + UDP receive.** `modules/lights/ArtnetInModule.h` listens on UDP 6454 and exposes received DMX as a `DataBuffer<RGB>` in the registry. Adds `receive` to `pal::Udp` (today's send-only API landed minimally; the second caller justifies it). `pal::Udp::send` already uses `AsyncUDP::writeTo`. Unlocks when a host running QLC+ is available for HIL.
- **NtpModule.** `modules/system/NtpModule.h` syncs via SNTP (arduino-esp32 has `configTime`; PC uses `chrono::system_clock`). Exposes `synced` / `last_sync_ts` controls; `pal::PalSystemInfo::local_time_str` already reads `std::time` — once `configTime` runs, that reports real wall-clock time. Lands together with ArtNet-in.
- **FirmwareUpdateModule (OTA).** `modules/system/FirmwareUpdateModule.h` accepts `POST /api/firmware` (binary upload) via `PalHttp::onPostBinary` (in place since [Sprint 2](release-01.md#sprint-2)); streams chunks to `esp_ota_*`, verifies on completion, sets boot partition, reboots. Includes a GitHub-release flow (control `release_url` → downloads asset → same OTA path). HIL: build firmware locally → upload via UI → device reboots into the new image → `SystemStatusModule.sketch_kb` shows the new size.
- **v1 → v2 cutover.** Visual + metrics parity check between v2 (this repo) and v1 (legacy) on the same `esp32dev`: 30 s capture (preview screenshot + Art-Net packet dump + `/api/system` heap/fps timeline) per side. Diff target: frames look the same (within rendering noise), Art-Net wire bytes match for the same effect at the same settings, heap within 20 %, fps within 10 %. Anything outside that range is a parity bug to fix or to document as an intentional v2 change. v1 (`projectMM`) gets final freeze + tag `v1.8.x-legacy` + README pointing to v2; this repo renames to `projectMM` (TBD path — see Open questions) and tags `v2.0.0`.

**Release 2 deferreds.** Multi-universe ArtNet-in (one universe per ArtNetInModule instance; add more instances to span more pixels); mDNS service advertisement for the Art-Net device; OTA code-signing + rollback on failed boot (needs anti-bricking design).

**Open questions.**

- Cutover path — rename `projectMM-v2` → `projectMM` (preserving v1 history under a `legacy/v1` branch in the new repo) or merge into the existing `projectMM` repo as `main` (preserving the rewrite history)? Decide before the cutover sprint.
- Should Release 3 exist? Candidates: layering / scenes / MIDI / DDP. None currently scoped.

### Light domain architecture

The architecture below describes the full light pipeline. It is a practical application of the module-grouping and multiple-input patterns described in [architecture/system.md — MoonModule](../architecture/system.md#moonmodule-the-contract) and [architecture/system.md — Layering](../architecture/system.md#layering). The current code (Sprint 14) is the degenerate single-effect case: `RipplesEffect` owns its own `DataBuffer<RGB>`; `PreviewModule` and `ArtnetOutModule` each hold a `DataBufferReader` pointing at it. That fallback remains valid until the LayoutLayer step lands.

#### Layer types

**EffectLayer** — groups one or more effect modules. The EffectLayer owns one shared `DataBuffer<RGB>`; all child effects write into that same full buffer in the order they are listed (each effect overwrites or blends on top of the previous). A lone effect owns its own buffer as today (easy starting point; add a layer later without touching the effect). When the EffectLayer is present, it drives `onAllocateMemory` top-down so children receive geometry from the layer rather than sizing it themselves. To composite two effects with independent tuning, use two separate EffectLayers — each with its own buffer — and let the DriverLayer blend across them.

**LayoutLayer** — stateless; owns no buffer. The sole authority on geometry and physical wiring. Provides:
- `width()`, `height()`, `depth()` — the 3D bounding box of the installation. Pixel positions are 3D; not every point in the box has a pixel (e.g. a ring in space, a sparse grid).
- `physical_count()` — explicit count of physical pixels; not necessarily `width × height × depth`.
- `map(logical_idx) → physical_idx` — index remapping for serpentine wiring, zigzag panels, non-rectangular arrangements.

When no LayoutLayer is wired, the system defaults to a 16×16×1 grid with 1:1 mapping. Effects never carry geometry themselves; geometry is always the LayoutLayer's job.

**DriverLayer** — owns one `DataBuffer<RGB>` sized from the LayoutLayer's `physical_count()`. Links to one or more EffectLayers. Each tick it initiates a read from each linked EffectLayer's buffer and applies that EffectLayer's pixel map table to write mapped pixels into its own output buffer. Enables multi-core parallelism (driver on core 1, effects on core 0); the output buffer is what `ArtnetOutModule`, `PreviewModule`, and the WS2812 driver read from.

Direct-read optimisation (no own buffer, no transform) remains available as a degenerate case when only one effect is linked and layout is 1:1 — but own buffer is the default.

#### Pixel map table

Each **EffectLayer** owns a `PixelMap[]` array. The DriverLayer does not hold the table; it asks each EffectLayer to apply its own mapping during the copy pass.

```cpp
struct PixelMap {
  uint32_t src_idx;   // index into the EffectLayer's virtual pixel buffer
  uint32_t dst_idx;   // index into the DriverLayer's physical output buffer
};
```

Built once in `onAllocateMemory` and rebuilt on `onUpdate` whenever the linked LayoutLayer or modifier set changes. Hot path: one table walk per EffectLayer, pure array iteration, no branching, no function calls.

#### Modifiers

Modifiers are per-EffectLayer and change the *virtual* geometry seen by the effect — not colour post-processing. Examples: **mirror** (effect runs at half width; one virtual pixel maps to two physical pixels side-by-side), **rotate** (effect runs in a rotated coordinate frame), **transpose** (swap axes). The modifier's geometric transformation is pre-computed into the pixel map table at rebuild time: if a mirror modifier halves the virtual width, the EffectLayer allocates a half-sized buffer and the map table fans each entry out to two destination indices. Hot path cost is zero — the table already encodes the result.

#### Implementation plan

Three steps, each shippable independently:

**Step 1 — LayoutLayer + geometry flow** (next sprint scope). Add `GridLayoutModule` implementing `width()`, `height()`, `depth()`, `physical_count()`, `map()`. Add `addChild()` to `MoonModule` / `ModuleManager`; parent calls `onAllocateMemory` on children after sizing its own buffer. Effects stop hard-coding geometry and receive it from their parent EffectLayer, which receives it from the linked LayoutLayer. Default when no LayoutLayer is wired: 16×16×1. Unlocks: geometry flows top-down; effects are geometry-agnostic; layout changes resize everything in one cold-path rebuild.

**Step 2 — EffectLayer grouping**. EffectLayer gains `addChild()` support: it owns one shared `DataBuffer<RGB>` and all child effects write into that same full buffer in the order they are listed. Each effect overwrites or blends on top of what the previous wrote. To composite two effects independently with different tuning, use two separate EffectLayers. EffectLayer builds and owns its own `PixelMap[]` (1:1 at this step; modifiers come later). Unlocks: multiple effects layered within one EffectLayer before the DriverLayer sees the result.

**Step 3 — DriverLayer**. DriverLayer module links to one or more EffectLayers and a LayoutLayer. Owns its own `DataBuffer<RGB>` sized to `physical_count()`. Each tick: for each linked EffectLayer, reads its buffer and applies its `PixelMap[]` to write into the output buffer. Multiple EffectLayers are blended (blend mode per layer). `ArtnetOutModule` and `PreviewModule` point their `source` at the DriverLayer's registry entry instead of directly at an effect. Modifier support: EffectLayer rebuilds its `PixelMap[]` to encode geometric transforms when modifiers change.

#### Design decisions

**`PixelMap.dst_idx` ownership.** The table lives on the EffectLayer but `dst_idx` points into the DriverLayer's output buffer, creating a coupling. This is intentional: the performance benefit (single flat array walk in the hot path, no second indirection) justifies it. The rebuild trigger is layout-driven, not driver-driven — whenever the LayoutLayer changes it triggers a rebuild of all dependent EffectLayer `PixelMap[]` tables; the DriverLayer itself does not trigger rebuilds.

**Effect geometry controls replaced by LayoutLayer.** Once a LayoutLayer is wired, it is the sole authority on geometry. Any width/height/depth controls on individual effects (e.g. `RipplesEffect`) are removed at Step 1; the effect receives its virtual dimensions top-down from its parent EffectLayer, which receives them from the LayoutLayer. There is no negotiation between effect controls and layout.

#### Deferred entries consolidated here

The following backlog entries from earlier sprints are superseded by this plan and are not tracked separately:

- *Parent modules + child trees* (Sprint 6) — covered by Step 1 (`addChild`) and Step 2 (EffectLayer grouping).
- *DataBufferModule — buffer as a named module* (Sprint 13) — EffectLayer is the right owner; a standalone `DataBufferModule` is not needed.
- *Effect layering / blending* (Sprint 6) — covered by Step 2 (within one EffectLayer) and Step 3 (across EffectLayers in DriverLayer).

**Step 1 landed in Sprint 15.** `GridLayoutModule` is the geometry authority; `RipplesEffect` receives dimensions top-down; `PreviewModule` and `ArtnetOutModule` update automatically. Step 2 (EffectLayer grouping) unlocks when multiple effects need to composite into one buffer.

#### Child dispatch timing — design note for Step 2

`MoonModule::runLoop()` currently recurses into children automatically after calling `loop()` (parent-first order). This is correct for geometry flow (parent sets dimensions, children read them) but wrong for compositing (children produce pixels, parent blends them — parent needs to run *after* children).

**Chosen approach for Step 2:** add `runChildren()` as a protected helper and a `childrenDispatched_` bool (fits in existing padding, zero overhead):

```cpp
// In runLoop(): after loop(), if (!childrenDispatched_) recurse; reset flag.
// In runChildren(): recurse now, set childrenDispatched_ = true.
```

An EffectGroup overrides `loop()`, calls `runChildren()` mid-method (between prepare and composite), and the framework skips the automatic pass because `childrenDispatched_` is already true. Non-overriding modules are unaffected — they never call `runChildren()`, so the flag stays false and the automatic recursion fires as before.

**Rejected alternative:** `loopBeforeChildren()` + `loopAfterChildren()` split — splits one logical operation across two methods, doesn't handle conditional or multi-pass child dispatch. **Also rejected:** removing automatic recursion entirely — every grouping module that doesn't override `loop()` would silently stop dispatching children; the bug is invisible at compile time.

**Not a Sprint 16 concern.** Land `runChildren()` + `childrenDispatched_` in the same sprint as EffectGroup (Step 2).

---

- **Per-module core affinity via UI control.** From [Sprint 7 deferred](release-01.md#sprint-7). `core_` is hardcoded per module class; making it a settable schema control lands when there's user demand for runtime remapping.
- **FastLED / WS2812 GPIO driver** (and `PalGpio.h` + typed board-config codegen). From Sprint 6 + 7 deferreds. Lands when a board with a strip is on the bench.
- **Pub/sub event bus.** From [Sprint 6 deferred](release-01.md#sprint-6). Registry + ring is enough today; revisit when many-to-many fan-out + selective updates demand it.

### Test infrastructure

- **Per-chip baseline JSON** (`deploy/test/scenario-baseline.json`). From [Sprint 8 deferred](release-01.md#sprint-8). Unlocks when a slow numeric regression slips past because today's number looked normal relative to last week. Introduce baseline diff for the one metric that drifted, not all metrics.
- **Devicelist + parallel orchestration.** From [Sprint 8](release-01.md#sprint-8) + [Sprint 10](release-01.md#sprint-10) deferreds. Unlocks when more than one device is tested every PR. Today: one s3 at 192.168.1.156, optionally an esp32dev at 192.168.1.234.
- **Committed `deploy/run/*.log` serial-log artefacts.** From [Sprint 8 deferred](release-01.md#sprint-8). Unlocks when a diff episode requires last week's serial output to spot today's drift. Default: read the log live in MoonDeck; no commit.
- **`scripts/scenario.py` bounds beyond `module_count`** (e.g. `fps_min`, `heap_free_min`). From [Sprint 10 deferred](release-01.md#sprint-10). Unlocks when a numeric regression slips past the current `module_count`-only assertion.
- **On-target unit tests.** From [Sprint 4 deferred](release-01.md#sprint-4). Promote when there's platform-divergent behaviour worth asserting on hardware that the in-process replay can't cover.

### MoonDeck / dev console

- **`scan_releases()` file-watcher.** From [Sprint 10 deferred](release-01.md#sprint-10). Currently re-scanned at process start only; restart MoonDeck to pick up new releases/sprints. Unlocks when the restart friction is felt.
- **End-user flash path via WebSerial** (ESP Web Tools / ESPConnect). From [Tools investigation](#tools-investigation-orchestration-alternatives-to-moondeck). Land when external contributors arrive (likely Release 2 cutover). Default reference is ESP Web Tools (Espressif-blessed `<esp-web-install-button>` component); ESPConnect is the polished bespoke version if more than flash is needed.
- **Firmware-in-WASM via Emscripten.** From [Tools investigation](#tools-investigation-orchestration-alternatives-to-moondeck). Land when shareable effect-demo URLs become a felt need (Wokwi-style). Not Release 1 scope.

### Editable canvas (node-graph editor)

Sprint 17 shipped the canvas as **read-only**: it renders the module tree as nested boxes + data-flow noodles, supports pan and a click-to-inspect sidebar, but all mutation (add / delete / reparent / edit controls) happens in the tree view only. The deliberate scope cut (per [Sprint 17 Step 3 DoD](release-01.md#sprint-17)) keeps the canvas a visualisation, not an editor.

**Unlock = make the canvas a first-class editor**, matching the tree view's mutation set:
- Drag a box onto another box → reparent (reuse `POST /api/modules/reparent`; the parent-input model already backs this).
- Delete a node from the canvas (reuse the tree's delete path).
- Add a module on the canvas (the add-module picker, currently tree-only in the side-nav).
- Draw a noodle to set a data-flow input (write the target id into the source's text control — the inverse of today's read-only noodle rendering).
- Editable controls in the canvas sidebar (today it shows values as static text; reuse `buildControl`).

When this lands, the side-nav's add-module button and the read-only sidebar become redundant for canvas users; revisit whether the tree view stays the default or becomes the "structural projection" companion to the canvas (see [system.md — Inputs, and the parent input](../architecture/system.md)). Not Release 1/2 scope; lands when the canvas is wanted as the primary authoring surface.

### System-module grouping (`system` input)

Sprint 17's parent-input model means a module can only be nested under a parent it has a matching input for (name == parent type, or the wildcard `source`). Light-domain modules have such inputs (`layout`, `source`); **system modules (`system-0`, `wifi-sta-0`, `http-0`, `ws-0`, `state-store-0`) have none**, so they cannot currently be grouped under one another. This was an original Sprint 17 DoD item, deferred: there is no current need to nest system modules (they work fine flat), and adding a speculative `system` input to every system module with no consumer is exactly the kind of just-in-case addition the [minimalism rule](../../CLAUDE.md) rejects.

**Unlock = a real need to group system modules** (e.g. a "network" group owning wifi + http + ws, or per-environment system bundles). Then: add a `system` input to the system-module base (or the specific modules that should be groupable); the existing match rule handles the rest with zero core change. Until then, system modules stay flat by design — *no implicit universal parent input exists* (that would re-introduce the dual concept Sprint 17 removed).

### WebSocket OOM crash on large displays

`WebSocketModule::broadcast_schema_()` and `broadcast_state_()` serialize JSON into static char buffers (Sprint 15 partial fix: `std::string` double-allocation removed). The remaining crash path: `JsonDocument` itself heap-allocates its DOM; on a large display with many modules, with the heap at ~37 KB free after the WiFi + lwIP stack takes its share, `operator new` inside ArduinoJson's pool allocator throws and `std::terminate` is called. `try/catch(std::bad_alloc)` in `PalWs.h` does not help because the throw happens before `broadcastText` is called.

**Root cause:** ArduinoJson 7's `JsonDocument` always heap-allocates; no stack-allocated alternative exists in v7. With 37 KB free and the largest contiguous block at 37 KB, the combined WiFi-stack churn + ArduinoJson pool allocation exceeds available contiguous memory.

**Unlock condition:** Either (a) replace `JsonDocument` with hand-rolled JSON serialization directly into the static output buffer (no intermediate DOM), or (b) reduce the number of modules / controls enough that the DOM fits in available memory at steady state, or (c) investigate ArduinoJson 6 `StaticJsonDocument<N>` as a stack-allocated alternative.

**Investigated and reverted:** Pre-allocated `std::string` members in `WebSocketModule`, `AsyncWebSocketSharedBuffer` persistent members in `PalWs.h` — both caused crashes on UI refresh (multiple simultaneous frame queues sharing one buffer, use-count race).

### Known patches — tracked for removal

Workarounds annotated `// PATCH:` in source. Each has a stated unlock condition; when the condition is met the patch and its comment are deleted together.

- **`PATCH: drag-guard` (`src/frontend/app.js`).** 2000 ms client-side guard prevents the 1 Hz backend push from overwriting a control the user is actively editing. Root cause: the WS push protocol has no "client owns this control" signal. Unlock: backend sends a client-lock or optimistic-update frame type, making the guard redundant.
- **`PATCH: schema-diff` (`src/frontend/app.js`).** Frontend diffs incoming schema to distinguish structural changes from value-only changes, avoiding a 1 Hz full DOM rebuild (which resets focus and flickers cards). Root cause: backend sends one `t:"schema"` event for both structure and value changes. Unlock: backend sends separate `schema-structure` vs `schema-values` event types.
- **`PATCH: queue-headroom` (`src/pal/PalWs.h`).** `canBroadcastBinary()` skips a pixel frame when the AsyncWebSocket queue is near-full, preventing 50 fps binary from starving 1 fps text messages. Root cause: AsyncWebSocket uses a single per-client queue for all frame types. Unlock: AsyncWebSocket separates binary/text queues, or the preview stream moves to a dedicated WebSocket endpoint.
- **`PATCH: wifi-guard` (`src/pal/PalUdp.h`).** Guards against pre-WiFi sends. Root cause: no lifecycle signal from the module graph when WiFi is up; the guard is a local workaround. Unlock: a WiFi-ready event from `WifiStaModule` removes the need for every caller to poll.

### Documentation / process

- **Recurring-evaluation sprint** (Release 5 per the Release Overview). From [Sprint 9 deferred](release-01.md#sprint-9). Framing is set in Release 1; concrete scope earns its place when Release 4 wraps.

---

## Parking lot

Investigations done and dropped. Recorded so the same alternatives are not re-evaluated without new information.

### Tools investigation — orchestration alternatives to MoonDeck

Post-Sprint-8 evaluation triggered by "are there alternatives to MoonDeck?". Recorded here so future deploy walks don't re-litigate the same options.

`scripts/moondeck.py`'s load-bearing role is the [§2 process-visibility rule](../architecture/process.md#2-guardrails-minimalism-enforced-mechanically): the developer-facing process surface is rendered as cards so adding or removing a script is visible work. Any alternative is measured against that, not just "does it run my build."

| Candidate | What it is | Outcome |
|---|---|---|
| `pi.dev` | Terminal AI coding-agent harness (Claude Code / Codex class) | **Different category** — alternative to the agent host, not to MoonDeck |
| VS Code `tasks.json` / JetBrains run configs | Editor-coupled command runners | **Complement, not substitute** — editor-specific, no editor-agnostic surface; hybrid pattern (tasks.json invokes `scripts/*.py`) keeps the single source of truth |
| `pio home` / PlatformIO IDE extension | Bundled-with-PlatformIO dashboard | **Insufficient** — covers pio commands; doesn't render custom scripts (mkdocs serve, classify_tests, scenario runs) → drift risk for everything outside pio |
| `just` / `Taskfile.dev` / `make` | CLI task runners with optional TUI pickers | **No surface visibility** — command palette out of sight by default; same v1 failure mode CLAUDE.md cites |
| `mprocs` / `process-compose` / `overmind` | Multi-process supervisors with TUI | **Shape mismatch** — for long-running processes (build watcher + serial + docs), not one-shot tasks; useful *alongside* MoonDeck if scope grows |
| Streamlit / Gradio / Marimo | Python → web UI frameworks | **Premature** — MoonDeck is small enough to stay hand-rolled; revisit when MoonDeck drift demands fewer LOC per card |
| `tmux` + shell scripts | Most minimalist; persistent panes via SSH | **Viable alternative** — drops the GUI; pure unix; perfect process visibility (every pane is a tab). Land if MoonDeck outgrows what one screen can show |
| WASM frontend (Yew / Leptos / Vugu) | Compile-to-WASM rendering of MoonDeck | **Overkill** — toolchain cost for a small UI; net negative under §1 |
| Compile firmware to WASM via Emscripten | Run the v2 light pipeline in a browser tab | **Orthogonal, future-interesting** — see [Planned → Firmware-in-WASM](#planned-for-next-releases) |
| WebSerial + `esptool-js` / **ESP Web Tools** | Browser flashes the device via the WebSerial API | **Future end-user path** — see [Planned → End-user flash path](#planned-for-next-releases) |
| **ESPConnect** ([repo](https://github.com/thelastoutpostworkshop/ESPConnect), [live](https://thelastoutpostworkshop.github.io/ESPConnect/)) | Polished Vue 3 + Vuetify + `tasmota-webserial-esptool` browser app; flash, backup, LittleFS/SPIFFS/FatFS/NVS browser | **Concrete reference for the WebSerial path** — 1.8 k★, MIT. LittleFS-from-browser feature is independently interesting for inspecting v2's `/state-*.json` on a flashed device |

**Decision:** **KEEP MoonDeck.** It's small, editor-agnostic, and the only candidate that renders the *project's own* custom scripts as a visible surface.

### DROPPED outcomes from Validated-during-Release-1

These were evaluated as Release-1 specifics and explicitly dropped. Reconsider only when the listed condition materialises.

- **Doc-growth budget number.** No automated count. Doc growth judged at review time. Reconsider when `docs/` drift becomes a felt problem.
- **`test_techdebt.cpp`-style encoded TODO tests.** Do not promote. v1 drift candidate (TODO list that fails CI tends to become permanent). Use ADRs with explicit closure dates instead.
- **`test_health_checks.cpp` / `healthReport()` meta-test.** Never landed; not needed. The test surface is small enough to read in one sitting; a meta-assertion buys nothing at that size. Reconsider only when the test surface exceeds the "readable in one sitting" threshold the v2 stance relies on.

### Status-doc aggregator

**`summarise.py` → `docs/status/index.md`.** From [Sprint 8 deferred](release-01.md#sprint-8). v1 had `deploy/summarise.py` that walked per-step `*.md` files and rendered an aggregate status page. v2 drops this until more than one human reads test results regularly. Today: one human. The page would be re-litigated only when "where do I see the latest test status?" gets asked by someone other than the maintainer.
