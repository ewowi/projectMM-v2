# Pal — files, budgets, who uses what

The architectural rule for Pal (one place for platform conditionals, partitioned by concern) lives in [system.md](../architecture/system.md#pal-the-only-place-platform-conditionals-appear). This page is the inventory: the current files, their LOC budgets, and which modules depend on which pal.

## Current files

Each file has its own [LOC budget](../architecture/process.md#2-guardrails-minimalism-enforced-mechanically) enforced by `scripts/check_loc.py`.

| File | Budget | Concern |
|---|---|---|
| `src/pal/Pal.h` | 100 | `millis`, `micros`, `sleep`, `yield`, `log_init`, `on_interrupt`; `default_http_port`, `default_scheduler_cores` |
| `src/pal/PalFs.h` | 150 | `fs_exists`, `fs_read_text`, `fs_write_text` (LittleFS on ESP32; `std::filesystem` under a `state/` sandbox on PC) |
| `src/pal/PalRtos.h` | 100 | `task_create_pinned` (`xTaskCreatePinnedToCore` on ESP32 with an explicit stack size; `std::thread` on PC, `core_id` ignored) |
| `src/pal/PalHeap.h` | 100 | `psram_alloc` / `psram_free`; PSRAM preferred (esp32s3), DRAM fallback (esp32dev / no-PSRAM). Both paths pin `MALLOC_CAP_8BIT` to guarantee byte-addressable memory — without it the allocator can hand back an IRAM-region buffer that faults on the first byte-wide write (LoadStoreError). See [`PalHeap.h`](https://github.com/ewowi/projectMM-v2/blob/main/src/pal/PalHeap.h)'s inline comment |
| `src/pal/PalWifi.h` | 100 | `wifi_begin` / `_is_connected` / `_disconnect`, `_local_ip`, `_rssi`, `_tx_power_dbm` / `_set_tx_power` |
| `src/pal/PalUdp.h` | 150 | `Udp { begin, send }` (`WiFiUDP` on ESP32; BSD `SOCK_DGRAM` + `SO_BROADCAST` on PC). Send-only today — receive lands when ArtNet-in does (Release 2) |
| `src/pal/PalHttp.h` | 350 | HTTP server (`cpp-httplib` on PC, `ESPAsyncWebServer` on ESP32) |
| `src/pal/PalWs.h` | 450 | WebSocket (POSIX sockets on PC, `AsyncWebSocket` on ESP32) |
| `src/pal/PalSystemInfo.h` | 250 | `chip_model`, `mac_address`, `reset_reason_str`; heap / psram / fs queries; `sketch_kb`, `build_info`, `core_temp`, `platform_version` |

Two shapes coexist: small platform *primitives* (`Pal`, `PalFs`, `PalRtos`, `PalHeap`, `PalWifi`, `PalUdp`) and larger *wrappers* around platform libraries or query surfaces (`PalHttp` / `PalWs` / `PalSystemInfo`). Both are "platform conditionals consolidated in one place" — that is what `pal/` means in v2, not "small enough to fit in one header."

## Module ↔ pal dependency

`Pal.h` (timing) is used everywhere — too pervasive to list per module. The table below covers the rest.

| Module | Pal files used |
|---|---|
| `core/MoonModule` | `Pal` (timing, `check_alloc`, `free_heap_bytes`) |
| `core/Scheduler` | `Pal` (timing, `yield`), `PalRtos` (`task_create_pinned`) |
| `modules/system/[SystemStatusModule](../user-guide/system/system-status.md)` | `PalSystemInfo` (chip / heap / fs / build info, all queries) |
| `modules/system/[WifiStaModule](../user-guide/system/wifi-sta.md)` | `PalWifi` (the whole `wifi_*` surface), `PalFs` (`/wifi.json` read/write) |
| `modules/system/[StateStoreModule](../user-guide/system/state-store.md)` | `PalFs` (`/modules.json` + per-module state files) |
| `modules/system/MemTracker` | `PalSystemInfo` (`free_heap_kb`, `free_psram_kb`, `max_alloc_kb`) |
| `modules/network/[HttpServerModule](../user-guide/network/http-server.md)` | `PalHttp` |
| `modules/network/[WebSocketModule](../user-guide/network/web-socket.md)` | `PalWs` |
| `modules/lights/[RipplesEffect](../user-guide/lights/ripples-effect.md)` | `PalHeap` (`psram_alloc` for `w·h·d` RGB) |
| `modules/lights/FrameRing` | `PalHeap` (depth-2 SPSC frame buffers) |
| `modules/lights/[ArtnetOutModule](../user-guide/lights/artnet-out.md)` | `PalUdp` |

## Deferred pal files

These pal headers will be added as the modules that need them land. Each addition requires a `BUDGETS` entry in `scripts/check_loc.py` and the rationale documented in [backlog.md](../development/backlog.md).

- **`PalGpio.h`** — lands when the FastLED / WS2812 driver does. Will host pin-mode, digital-write, and platform-specific RMT / I2S setup.
- **Receive-side UDP** (added to `PalUdp.h`) — lands with the ArtNet-in module in Release 2.

## Adding a new pal file

A new pal file is a *structural addition* and follows the same rule as adding a new top-level path: paired ADR under [`docs/developer-guide/adr/`](adr/0001-vendor-cpp-httplib.md), entry in `scripts/check_loc.py` `BUDGETS`, and a line above in the "Current files" table. The bar is "this concern cannot be expressed by reusing an existing pal file" — see [process architecture → anti-drift](../architecture/process.md).
