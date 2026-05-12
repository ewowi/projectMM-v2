# Release 2 — v1 parity + cutover

Release 2 closes the v1 → v2 transition. Release 1 shipped a working device with the light domain (RipplesEffect + Preview + Art-Net out), LittleFS state persistence, and a stress-tested two-core / PSRAM scaling path. Release 2 fills in the v1 features that didn't make Release 1 and culminates in renaming this repository from `projectMM-v2` to `projectMM` with the first stable `v2.0.0` tag.

## Sprints

| Sprint | Goal | Minimalism target |
|--------|------|------------------|
| Sprint 8 | ArtNet **in** + NTP wall-clock — first network-consumer light module + first time service | per-module ≤ 300 LOC |
| Sprint 9 | OTA firmware update + GitHub-release flash path | per-module ≤ 300 LOC |
| Sprint 10 | v1 parity check + cutover: visual + metrics parity on hardware, v1 tagged `v1.8.x-legacy`, repo renamed, v2 tagged `v2.0.0` | — |

The order is deliberate: ArtNet-in lets the device be driven from real DMX consoles before OTA changes how firmware gets onto it. NTP comes free with the WiFi-stack already running. OTA needs filesystem persistence (Sprint 6) + WiFi (Sprint 5) — both ready.

---

## Sprint 8 — ArtNet in + NTP {#sprint-8}

> **Scope:** First **inbound** Art-Net path, complementing Sprint 6's outbound module. `ArtNetInModule` listens on UDP 6454 and exposes received DMX as a `PixelSource` (so Preview and any downstream effect can consume it just like a local effect). `NtpModule` syncs time via the Sprint 5 WiFi stack and exposes `local_time` to `SystemStatusModule`.

### Definition of Done

- [ ] `pal::Udp` gains `receive` support (Sprint 6 was send-only — first caller landed it that minimal way per Rule #1; the second caller, ArtNet-in, justifies adding receive).
- [ ] `modules/lights/ArtnetInModule.h` extends `MoonModule` + `PixelSource`. Controls: `universe` (0..15, default 0), `width` / `height` / `depth` (1..128). Owns its own RGB buffer; received DMX bytes copied in chunks indexed by incoming universe number. `revision_++` on any incoming packet sequence advance.
- [ ] `modules/system/NtpModule.h` syncs via SNTP (arduino-esp32 has `configTime`; PC uses `chrono::system_clock`). Exposes `synced` / `last_sync_ts` controls. `pal::PalSystemInfo::local_time_str` already reads `std::time` — once `configTime` runs, that now reports real wall-clock time.
- [ ] HIL: drive ArtNet-in from a host running QLC+ → see live RGB on the device's Preview. SystemStatusModule's `local_time` shows correct local time after a few seconds.

### Deferred

- [ ] Multi-universe ArtNet-in (currently one universe per ArtNetInModule instance — to span more pixels, add more instances).
- [ ] mDNS service advertisement for the Art-Net device.

---

## Sprint 9 — OTA firmware update {#sprint-9}

> **Scope:** Replace the USB-flash path with over-the-air firmware updates. `FirmwareUpdateModule` accepts a `.bin` upload from the frontend (already has the UI per Sprint 3 step 4's verbatim port) and writes it to the inactive OTA partition. Also: a GitHub-release flash path that downloads + verifies the latest release asset matching the board.

### Definition of Done

- [ ] `modules/system/FirmwareUpdateModule.h` accepts `POST /api/firmware` (binary upload) via the existing `PalHttp::onPostBinary` (already in place since Sprint 2). Streams chunks to `esp_ota_*` APIs; verifies on completion; sets boot partition; reboots.
- [ ] GitHub-release flow: control `release_url` → downloads asset → same OTA flow.
- [ ] HIL: build a new firmware locally → upload via UI → device reboots into the new image → `SystemStatusModule.sketch_kb` shows the new size. Repeat from a GitHub-release tag.

### Deferred

- [ ] Code-signing / rollback on failed boot — needs anti-bricking thought.

---

## Sprint 10 — v1 parity + cutover {#sprint-10}

> **Scope:** Visual + metrics parity check between v2 (this repo) and v1 (the legacy repo) on the same hardware. After the check passes, the v1→v2 cutover happens.

### Definition of Done

- [ ] On the same `esp32dev`, boot v1 + record a 30 s capture (preview screenshot + Art-Net packet dump + `/api/system` heap/fps timeline). Repeat on v2. Diff: visual frames look the same (within rendering noise), Art-Net wire bytes match for the same effect at the same settings, heap usage is in the same ballpark (within 20 %), fps within 10 %. Anything outside that range is a parity bug to fix or to document as an intentional v2 change.
- [ ] v1 (`projectMM`) repo: final feature freeze, tag `v1.8.x-legacy`, README points to v2.
- [ ] This repo: renamed from `projectMM-v2` to `projectMM` (or merged into the legacy repo as the main branch — TBD), tagged `v2.0.0`.

---

## Open questions

- Where does the cutover land — rename `projectMM-v2` to `projectMM` (preserving v1 history under a `legacy/v1` branch in the new repo) or merge into the existing `projectMM` repo as `main` (preserving the rewrite history)? Decide before Sprint 10.
- Should Release 3 exist? If yes: candidates are layering / scenes / MIDI / DDP — none currently scoped.
