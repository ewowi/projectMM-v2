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

- **ArtNet-in module + UDP receive + AsyncUDP migration.** `modules/lights/ArtnetInModule.h` listens on UDP 6454 and exposes received DMX as a `DataBuffer<RGB>` in the registry. Adds `receive` to `pal::Udp` (today's send-only API landed minimally; the second caller justifies it). At the same point, migrate `pal::Udp::send` from `WiFiUDP::endPacket` to `AsyncUDP::writeTo` — fire-and-forget, no lwIP blocking on the calling task, eliminates the `endPacket(): could not send data` error spam when the ArtNet destination is unreachable (confirmed in Sprint 13 HIL: `WiFiUDP` with a bad `dest_ip` floods the WiFi task with EHOSTUNREACH at 50 fps). MoonLight and WLED-MM both use `AsyncUDP` for this reason. Unlocks when a host running QLC+ is available for HIL.
- **NtpModule.** `modules/system/NtpModule.h` syncs via SNTP (arduino-esp32 has `configTime`; PC uses `chrono::system_clock`). Exposes `synced` / `last_sync_ts` controls; `pal::PalSystemInfo::local_time_str` already reads `std::time` — once `configTime` runs, that reports real wall-clock time. Lands together with ArtNet-in.
- **FirmwareUpdateModule (OTA).** `modules/system/FirmwareUpdateModule.h` accepts `POST /api/firmware` (binary upload) via `PalHttp::onPostBinary` (in place since [Sprint 2](release-01.md#sprint-2)); streams chunks to `esp_ota_*`, verifies on completion, sets boot partition, reboots. Includes a GitHub-release flow (control `release_url` → downloads asset → same OTA path). HIL: build firmware locally → upload via UI → device reboots into the new image → `SystemStatusModule.sketch_kb` shows the new size.
- **v1 → v2 cutover.** Visual + metrics parity check between v2 (this repo) and v1 (legacy) on the same `esp32dev`: 30 s capture (preview screenshot + Art-Net packet dump + `/api/system` heap/fps timeline) per side. Diff target: frames look the same (within rendering noise), Art-Net wire bytes match for the same effect at the same settings, heap within 20 %, fps within 10 %. Anything outside that range is a parity bug to fix or to document as an intentional v2 change. v1 (`projectMM`) gets final freeze + tag `v1.8.x-legacy` + README pointing to v2; this repo renames to `projectMM` (TBD path — see Open questions) and tags `v2.0.0`.

**Release 2 deferreds.** Multi-universe ArtNet-in (one universe per ArtNetInModule instance; add more instances to span more pixels); mDNS service advertisement for the Art-Net device; OTA code-signing + rollback on failed boot (needs anti-bricking design).

**Open questions.**

- Cutover path — rename `projectMM-v2` → `projectMM` (preserving v1 history under a `legacy/v1` branch in the new repo) or merge into the existing `projectMM` repo as `main` (preserving the rewrite history)? Decide before the cutover sprint.
- Should Release 3 exist? Candidates: layering / scenes / MIDI / DDP. None currently scoped.

### Light domain

- **Parent modules + child trees (`addChild`).** From [Sprint 6 deferred](release-01.md#sprint-6). Unlocks when effect-on-effect composition arrives (Effect layering depends on this).
- **DataBufferModule — buffer as a named module.** From [Sprint 13](release-01.md#sprint-13). Today the producer module (e.g. RipplesEffect) owns its `DataBuffer`: buffer lifecycle is tied to effect lifecycle, which is the right default. A dedicated `DataBufferModule` would own the buffer independently — enabling multiple effects writing into it (layering) and resizing geometry without touching the effect. Unlocks when effect layering is on the roadmap; `DataRegistry` is already the indirection layer needed to make it clean. See [architecture/system.md — Layering](../architecture/system.md#layering) and [developer-guide/backend.md — Layering](../developer-guide/backend.md#layering).
- **Per-module core affinity via UI control.** From [Sprint 7 deferred](release-01.md#sprint-7). `core_` is hardcoded per module class; making it a settable schema control lands when there's user demand for runtime remapping.
- **FastLED / WS2812 GPIO driver** (and `PalGpio.h` + typed board-config codegen). From Sprint 6 + 7 deferreds. Lands when a board with a strip is on the bench.
- **Effect layering / blending.** From [Sprint 6 deferred](release-01.md#sprint-6). Comes with parent modules.
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

### Known patches — tracked for removal

Workarounds annotated `// PATCH:` in source. Each has a stated unlock condition; when the condition is met the patch and its comment are deleted together.

- **`PATCH: drag-guard` (`src/frontend/app.js`).** 2000 ms client-side guard prevents the 1 Hz backend push from overwriting a control the user is actively editing. Root cause: the WS push protocol has no "client owns this control" signal. Unlock: backend sends a client-lock or optimistic-update frame type, making the guard redundant.
- **`PATCH: schema-diff` (`src/frontend/app.js`).** Frontend diffs incoming schema to distinguish structural changes from value-only changes, avoiding a 1 Hz full DOM rebuild (which resets focus and flickers cards). Root cause: backend sends one `t:"schema"` event for both structure and value changes. Unlock: backend sends separate `schema-structure` vs `schema-values` event types.
- **`PATCH: queue-headroom` (`src/pal/PalWs.h`).** `canBroadcastBinary()` skips a pixel frame when the AsyncWebSocket queue is near-full, preventing 50 fps binary from starving 1 fps text messages. Root cause: AsyncWebSocket uses a single per-client queue for all frame types. Unlock: AsyncWebSocket separates binary/text queues, or the preview stream moves to a dedicated WebSocket endpoint.
- **`PATCH: wifi-guard + WiFiUDP` (`src/pal/PalUdp.h`).** Guards against pre-WiFi sends and EHOSTUNREACH spam from `WiFiUDP::endPacket()` when the destination is unreachable. Root cause: `WiFiUDP` is blocking and logs errors on every failed send (50 fps × bad `dest_ip` = log flood). Unlock: AsyncUDP migration (Release 2 — ArtNet-in).

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
