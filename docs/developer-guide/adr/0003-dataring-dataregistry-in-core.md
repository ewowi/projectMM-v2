# ADR 0003 — `DataRing<T>` and `DataRegistry` in core

**Status:** Accepted  
**Sprint:** 13  
**Date:** 2026-05-14

## Context

`FrameRing` (Sprint 7) and `PixelRegistry` (Sprint 6) live in `src/modules/lights/`. Sprint 13 replaces both with generic types. The question is whether those generic types belong in `src/core/` or stay in `src/modules/`.

The core boundary from [architecture/system.md](../../architecture/system.md):

> **In core:** `Module`, `ModuleManager`, `Scheduler`, `Pal` (timing / GPIO / fs / rtos primitives / heap query — nothing more).  
> **Not in core:** networking, HTTP / WebSocket / REST, OTA, NTP, state persistence, lighting.

## Decision

`DataRing<T>` and `DataRegistry` are placed in `src/core/`.

**Justification:**

- `DataRing<T>` is a concurrency primitive — a depth-configurable SPSC ring with acquire/release memory ordering. It is as domain-neutral as `std::atomic`. It belongs alongside `Scheduler` which already manages cross-core task dispatch.
- `DataRegistry` is a typed singleton store keyed by string id. It is structurally identical to `ModuleManager` (owns objects, resolves by name) and belongs in the same layer.
- Neither type references `RGB`, pixel geometry, lighting, networking, or any other domain. The template parameter `T` is supplied by the leaf module (`RipplesEffect` uses `DataRing<RGB>`); core never sees `RGB`.
- The alternative — keep them in `src/modules/lights/` — would mean a future audio or sensor module wanting the same SPSC pattern would either duplicate the ring or take a dependency on the lights module. That is the v1 drift pattern.

## Consequences

- `src/core/` gains two new files: `DataRing.h` and `DataRegistry.h`.
- `src/modules/lights/FrameRing.h` and `PixelRegistry.h` are deleted.
- `check_loc.py` core budget must be bumped to account for the two new files. The bump is explicit and PR-visible, consistent with the minimalism rule.
- Future non-lights modules that need SPSC producer/consumer buffers use `DataRing<T>` directly from core — no cross-domain dependency.
