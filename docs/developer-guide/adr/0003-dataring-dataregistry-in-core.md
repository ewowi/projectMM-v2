# ADR 0003 — `DataBuffer<T>` and `DataRegistry` in core

**Status:** Accepted  
**Sprint:** 13 (landed as `DataRing<T>`); revised same release to `DataBuffer<T>` (ring concept removed — see note below)  
**Date:** 2026-05-14

## Context

`FrameRing` (Sprint 7) and `PixelRegistry` (Sprint 6) live in `src/modules/lights/`. Sprint 13 replaces both with generic types. The question is whether those generic types belong in `src/core/` or stay in `src/modules/`.

The core boundary from [architecture/system.md](../../architecture/system.md):

> **In core:** `Module`, `ModuleManager`, `Scheduler`, `Pal` (timing / GPIO / fs / rtos primitives / heap query — nothing more).  
> **Not in core:** networking, HTTP / WebSocket / REST, OTA, NTP, state persistence, lighting.

## Decision

`DataBuffer<T>` and `DataRegistry` are placed in `src/core/`.

**Justification:**

- `DataBuffer<T>` is a concurrency primitive — a single-slot SPSC buffer with acquire/release memory ordering. It is as domain-neutral as `std::atomic`. It belongs alongside `Scheduler` which already manages cross-core task dispatch.
- `DataRegistry` is a typed singleton store keyed by string id. It is structurally identical to `ModuleManager` (owns resolution by name) and belongs in the same layer.
- Neither type references `RGB`, pixel geometry, lighting, networking, or any other domain. The template parameter `T` is supplied by the leaf module (`RipplesEffect` uses `DataBuffer<RGB>`); core never sees `RGB`.
- The alternative — keep them in `src/modules/lights/` — would mean a future audio or sensor module wanting the same SPSC pattern would either duplicate the buffer or take a dependency on the lights module. That is the v1 drift pattern.

**Note on ring depth.** The original Sprint 13 design used a depth-configurable ring (`DataRing<T>`, `allocate(count, depth)`). This was removed in the same release: a ring with depth > 1 *within one module* is overdesign — the double-buffer boundary belongs between two separate modules (producer owns one `DataBuffer<T>` slot, consumer owns its own `DataBuffer<T>` slot). The file was renamed `DataBuffer.h` and the depth parameter dropped. See [architecture — hot-path data sharing](../../architecture/system.md#hot-path-data-sharing-between-modules) and [backend — data sharing](../../developer-guide/backend.md#data-sharing) for the rationale.

## Consequences

- `src/core/` gains two new files: `DataBuffer.h` and `DataRegistry.h`.
- `src/modules/lights/FrameRing.h` and `PixelRegistry.h` are deleted.
- `check_loc.py` core budget must be bumped to account for the two new files. The bump is explicit and PR-visible, consistent with the minimalism rule.
- Future non-lights modules that need SPSC producer/consumer buffers use `DataBuffer<T>` directly from core — no cross-domain dependency.
