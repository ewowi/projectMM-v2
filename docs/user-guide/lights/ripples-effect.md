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

- `setup()` — publish `this` to `PixelRegistry` so consumers can find it by id.
- `onAllocateMemory()` — call `allocate_()` which `psram_alloc`s three buffers: pixel buffer (`w·h·d`), phase-offset Q16 table (`w·h`), base-color full-bright RGB table (`w·h`). Allocates the `FrameRing` slots (two more pixel-buffer-sized chunks for the cross-core consumer). Bumps `revision_`.
- `onUpdate(key)` — `width` / `height` / `depth` re-call `allocate_()`; `hue_base` re-calls `rebuild_color_table_()` (skips the phase table — only color changes).
- `loop20ms()` — compute one frame using the precomputed tables + cos LUT. Publish a copy into the cross-core SPSC `FrameRing` for [ArtnetOutModule](artnet-out.md). Same-core consumers ([PreviewModule](preview.md)) read `pixels_` directly via `pixelBuffer()`.
- `teardown()` — unpublish from `PixelRegistry`, free pixel + table buffers.
- `pixelBuffer()` / `frameRing()` — `PixelSource` interface methods consumers use to read the frame.

### Pixel-buffer sharing — why a registry, not `dynamic_cast`

The natural shape for a consumer to grab a producer's frame would be `dynamic_cast<PixelSource*>(manager_->find(id))`. That does not work on hardware: arduino-esp32 builds with `-fno-rtti`. Adding `-frtti` is a hammer (binary size, framework-wide); adding a virtual `asPixelSource()` to `MoonModule` would put light-domain knowledge in core. The minimal answer is a publish-on-setup / find-by-id registry that lives entirely in `modules/lights/` ([`PixelRegistry.h`](https://github.com/ewowi/projectMM-v2/blob/main/src/modules/lights/PixelRegistry.h), ~40 LOC).

This is publish/subscribe — just the cheap version. Publish happens once in `setup()`, subscribe (`find`) happens in `setup()` and on `onUpdate("source")`, with a fallback in `loop20ms` to tolerate consumers that load before producers. The hot path is a cached pointer + one virtual call + one `uint32` revision compare. No event bus, no per-tick dispatch, no allocation. Same-core consumers (PreviewModule) read `pixels_` directly via `pixelBuffer()`. Cross-core consumers (ArtnetOutModule on core 1) read via the depth-2 SPSC `FrameRing` with release/acquire ordering — producer never waits; on full it overwrites, dropping the older frame.
