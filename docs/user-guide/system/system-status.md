# SystemStatusModule

Aggregates device-wide runtime status — CPU, heap, PSRAM, filesystem, firmware build info — and exposes it as read-only controls. The frontend reads these to render the system card; `/api/system` returns the same fields as JSON. Always present (head module: added by `main.cpp` before user-added modules).

## End-user reference

| Control | Type | Range / default | Notes |
|---|---|---|---|
| `enabled` | toggle | true | Disables all loops |
| `local_time` | display | — | `HH:MM:SS` local time |
| `uptime_s` | time | 0..2 000 000 | Seconds since boot |
| `fps` | display | 0..2 000 000 | Scheduler loop rate |
| `heap_used_kb` / `heap_free_kb` | progress / display | 0..total | Internal SRAM |
| `max_alloc_kb` / `max_alloc_used_kb` | display / progress | 0..total | Largest free contiguous block |
| `heap_min_kb` | display | — | Lifetime min free heap |
| `heap_frag_pct` | display | 0..100 | `(1 − largest/free) × 100` |
| `psram_used_kb` / `psram_free_kb` / `total_psram_kb` | progress / display / display | 0..total | ESP32-S3 only |
| `psram_mode` | display | `spiram` / `none` | |
| `fs_used_kb` / `fs_free_kb` | progress / display | 0..total | LittleFS partition |
| `firmware_kb` | progress | 0..partition size | App image size |
| `core_temp` | display | -40..125 °C | ESP32 only |
| `reset_reason`, `firmware_version`, `env`, `build_date`, `build_time`, `chip_model`, `mac_address`, `platform_version` | display | — | Build identity |

## Developer reference

- `setup()` — read static build identity (`pal::chip_model`, `mac_address`, `total_heap_kb`, `total_psram_kb`, `cpu_freq_mhz`, etc.) into instance fields so `onBuildControls()` can pass them straight to `addControl()`.
- `loop1s()` — re-sample dynamic fields (free heap, fragmentation, fps, core temp, uptime, fs usage) and call `memtracker::tick_1s()` to emit `[MemLive]` heartbeat events.
- `fillSystemJson()` — contribute every status field to `/api/system`'s aggregated JSON (called by `HttpServerModule`).
