# RipplesEffect

Radial sine ripples on a 2D RGB panel (with optional repeated depth slices). Computes one frame per `loop20ms` tick using a precomputed phase table + base-color table + 256-entry cosine LUT — the per-pixel inner loop is one Q16 subtract, one LUT load, three uint8 mul-shifts. No `sqrt` / `cos` / HSV per pixel; see [Sprint 7's perf tune](../../development/release-01.md#sprint-7). Publishes its frame via `PixelRegistry` so consumers ([PreviewModule](preview.md), [ArtnetOutModule](artnet-out.md)) can read it by source id.

## End-user reference

| Control | Type | Range / default | Notes |
|---|---|---|---|
| `enabled` | toggle | true | |
| `width` | slider | 1..128 / 16 | Geometry; change reallocates pixel + table buffers |
| `height` | slider | 1..128 / 16 | Geometry; change reallocates |
| `depth` | slider | 1..16 / 1 | Slice count (2D pattern repeated per slice) |
| `speed` | slider | 0.1..10 / 1.0 | Radians/sec the wave advances |
| `hue_base` | slider | 0..1 / 0.6 | Base hue; rotates +0.05/distance for visible bands. Change recolours without reallocating. |

## Developer reference

- `onAllocateMemory()` — call `allocate_()` which `psram_alloc`s three buffers: working pixel buffer (`w·h·d`), phase-offset Q16 table (`w·h`), base-color full-bright RGB table (`w·h`). Allocates one [`DataBuffer<RGB>`](../../developer-guide/backend.md#databuffert--the-shared-slot-primitive) slot (one pixel-buffer-sized chunk); declares it in `DataRegistry` under `id()`.
- `onUpdate(key)` — `width` / `height` / `depth` re-call `allocate_()`; `hue_base` re-calls `rebuild_color_table_()` (skips the phase table — only color changes).
- `loop20ms()` — compute one frame into the working pixel buffer using the precomputed tables + cos LUT; then `buf_->acquire_write()` + memcpy + `buf_->publish()` so consumers ([ArtnetOutModule](artnet-out.md), [PreviewModule](preview.md)) can read it cross-core.
- `teardown()` — `DataRegistry::instance().undeclare(buf_)`, free pixel + table buffers.

### Pixel-buffer sharing — why a registry, not `dynamic_cast`

The natural shape for a consumer to grab a producer's frame would be `dynamic_cast<RipplesEffect*>(manager_->find(id))`. That does not work on hardware: arduino-esp32 builds with `-fno-rtti`. Adding `-frtti` is a hammer (binary size, framework-wide); putting a type-specific accessor on `MoonModule` would put light-domain knowledge in core. The minimal answer is `DataRegistry` — a publish-on-`onAllocateMemory` / resolve-by-id registry in `src/core/` that is domain-neutral (see [ADR 0003](../adr/0003-dataring-dataregistry-in-core.md)).

This is publish/subscribe — just the cheap version. Declare happens once in `onAllocateMemory()`; resolve (`DataRegistry::resolve`) happens in consumer `setup()` and on `onUpdate("source")`, with a fallback in `loop20ms` to tolerate late producers. The hot path is a cached pointer + one atomic load + one `uint32` revision compare. No event bus, no per-tick dispatch, no allocation. See [architecture — hot-path data sharing](../../architecture/system.md#hot-path-data-sharing-between-modules) and [backend — data sharing](../../developer-guide/backend.md#data-sharing).
