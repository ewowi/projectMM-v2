# System Architecture

The core is four pieces.

```
MoonModule                          — the contract: lifecycle + controls + identity
ModuleManager                       — owns MoonModule instances
Scheduler                           — runs the DAG across cores
Pal                                 — platform abstraction, nothing more
```

This page is the constraint. Any pull request that does not fit the picture below is either rejected or carries a paired ADR file (`docs/developer-guide/adr/NNNN-*.md`) recording the architecture change explicitly. See [process architecture](process.md) for why.

---

## MoonModule — the contract

```cpp
class MoonModule {
public:
  // Lifecycle (all optional, empty defaults)
  virtual void setup()             {}     // construct state, wire collaborators
  virtual void loop()              {}     // hot path: bounded time, no alloc, no block
  virtual void loop20ms()          {}     // sub-hot responsiveness: periodic, lower urgency
  virtual void loop1s()            {}     // monitoring, health
  virtual void loop10s()           {}     // housekeeping, persistence
  virtual void teardown()          {}     // free every allocation made in setup / onAllocateMemory

  // Hooks driven by the runtime around setup()
  virtual void onBuildControls()   {}     // module's addControl() calls go here
  virtual void onAllocateMemory()  {}     // dynamic buffers, sized from final control values
  virtual void onUpdate(const char* key) {} // a control value changed (frontend edit, loadState, etc.)

  // Controls (lazy: a module that never calls addControl pays no overhead)
  void addControl(T& field, const char* name, const char* uiKind,
                  float min = 0, float max = 0);  // bind a field, expose in schema
};
```

The contract is the entire module-facing API of the runtime. Six lifecycle virtuals plus three hooks (`onBuildControls`, `onAllocateMemory`, `onUpdate`) plus the control-system entry point — all optional, all empty by default. A module that overrides nothing is a no-op; a module with controls overrides `onBuildControls()` (not `setup()`) to call `addControl()`; in both cases, the module pays only for what it uses.

**Why three setup-time hooks?** The runtime drives them in a fixed order: `setup()` → `onBuildControls()` → (recurse into children) → `onChildrenReady()` → `onAllocateMemory()`. Putting `addControl()` calls in `onBuildControls` instead of `setup()` lets the runtime call it again standalone (hot-reload, schema rebuild after a type change). `onAllocateMemory` runs *after* controls have been seeded from persisted state, so a module sizing its buffer from a control value (`RipplesEffect` allocating `w·h·d` RGB) sees the right value on the first try. `onUpdate(key)` fires when a control's value changes at runtime; that's where reallocation triggers live (geometry change → re-run `onAllocateMemory`).

**One class, not two.** v1 split `Module` and `StatefulModule` into separate types — v2 collapsed them because every real module in v2 ends up using controls (settable parameters, displayed metrics, schema for the WebSocket frontend). The "plain Module without controls" case was theoretical in v1 and absent in v2's plan; the cost of carrying the control system on every module is byte-level (an empty pointer, lazily filled on first `addControl`). The simplification removes an artificial concept boundary.

**Hot path.** `loop()` is the load-bearing decision in the runtime: it must do as little as possible at the maximum frequency the platform allows. The guardrails enforce this mechanically — no allocations, no blocking calls, no logging in any `loop*()` body — see [process architecture](process.md). `addControl` and dynamic-buffer allocation belong in `onBuildControls` / `onAllocateMemory` (or in `onUpdate` on a real configuration change); never in `loop*()`.

**Tiered cadences.** `loop20ms`, `loop1s`, and `loop10s` exist to drain less urgent work out of the hot path at progressively lower rates so `loop()` stays short. A module decides for itself which cadences it needs; the scheduler pays nothing for cadences a module does not override.

**Control system.** `addControl(field, name, uiKind)` binds a backing field to a named, schema-described control. The `WebSocketModule` serialises the schema to clients on connect; incoming control updates from clients are dispatched back into the bound field, then `onUpdate(key)` fires so the owning module can react (re-allocate, rebuild a derived table, persist). UI kinds drive frontend rendering (`"display"`, `"progress"`, `"toggle"`, `"slider"`, `"text"`, etc.). The full per-module control list lives in the [User Guide](../user-guide/index.md) — one page per module, end-user + developer reference per page.

**Multi-core.** The runtime is built to exploit every core the platform offers. Several `loop()` instances run in parallel, connected as a DAG. The scheduler pins a separate task per core and arranges the topology declared at wire time.

`MoonModule` total target: ≤ 600 LOC. v1's `Module` + `StatefulModule` together is ~996 LOC; v2's minimized merger lands smaller.

---

## ModuleManager — instance ownership

```cpp
class ModuleManager {
public:
  MoonModule* add(const char* type, const char* id);
  bool        remove(const char* id);
  bool        replace(const char* id, const char* newType);
  void        wire(const char* fromId, const char* toId);
};
```

ModuleManager owns `MoonModule` instances (`std::unique_ptr`). It exposes structural operations only: add, remove, replace, wire. **It does not** parse REST requests, serialise to JSON, write state files, debounce dirty flags, or accumulate memory accounting. Those are jobs of separate modules (`HttpServerModule`, `StateModule`, etc.) that depend on the ModuleManager, not the other way around.

The name `ModuleManager` is preserved (not renamed `MoonModuleManager`) — it manages instances of `MoonModule`; the slight asymmetry is honest about its job and saves a noisy prefix.

---

## Scheduler — DAG runner across cores

```cpp
class Scheduler {
public:
  void connect(Module* producer, Module* consumer);  // declare a DAG edge
  void run();                                        // start pinned tasks
};
```

The Scheduler holds the DAG. Each edge is a single-producer-single-consumer (SPSC) lock-free ring buffer; the producer's `loop()` writes one item, the consumer's `loop()` reads one item, no other thread touches either. The ring buffer is the data-crossing mechanism between cores; depth defaults to 2 and is configurable per edge.

**Topology.** Arbitrary DAG. A linear pipeline is the trivial case (one producer, one consumer). Fan-out is supported (one producer, several consumers). Cycles are rejected at wire time.

**Pinning.** One pinned task per core. The scheduler distributes Module `loop()` calls across cores; a Module's preferred core is a hint, not a guarantee.

---

## Pal — the only place platform conditionals appear

Pal is not one file. It is a directory of small, single-concern headers. The defining rule:

> **`#ifdef ARDUINO`, `#include <Arduino.h>`, ESP-IDF includes, and every other platform-identity gate live in `src/pal/` and nowhere else.** Modules see only `pal::*` calls; modules contain zero platform conditionals.

This is enforced mechanically by `scripts/check_platform_guards.py`: any platform guard outside `src/pal/` fails CI. Porting v2 to a new platform means writing new pal files; it never means touching a module.

The drift this rule guards against is twofold:

- **v1's kitchen-sink Pal.h** — a single file that swelled because every new concern just got appended. v2 prevents this by mandating one pal *file* per concern, each with its own LOC budget. Adding a new concern adds a new file (subject to the structural-additions rule) plus a new entry in `scripts/check_loc.py`.
- **v2's first-pass overcorrection** — banning system info, HTTP, etc. *from* Pal. That ban scattered `#ifdef ARDUINO` blocks across every module that touched the platform, which is the worst of both worlds. The right rule is to keep platform code consolidated in `pal/` *and* keep `pal/` partitioned by concern.

**Test surface.** Each pal file is mockable for tests: the file declares the `pal::*` interface, and platform-conditional implementations behind `#ifdef ARDUINO` provide the bodies. A test build can stub the same interface with a fake. This is why pal files are the *only* place platform code lives: every other module gets the abstraction, not the conditional.

The current per-file inventory (budgets + concerns), a module ↔ pal cross-reference, and the deferred-pal list live in [developer-guide/pal.md](../developer-guide/pal.md). That page churns release-by-release as new pals land; the rule on this page does not.

---

## What is not in the core

| Domain | Belongs to |
|--------|------------|
| HTTP server platform layer | `pal/PalHttp.h` |
| WebSocket platform layer | `pal/PalWs.h` |
| System info reads (chip, reset reason, sketch size) | `pal/PalSystemInfo.h` |
| HTTP server **module** (route registration, request dispatch) | `modules/network/HttpServerModule` |
| WebSocket **module** (schema push, state broadcast) | `modules/network/WebSocketModule` |
| WiFi / Ethernet / mDNS modules | `modules/network/` |
| OTA firmware updates | `modules/firmware/` |
| NTP wall-clock sync, system status display module | `modules/system/` |
| State persistence (LittleFS, JSON files) | `modules/state/` |
| Lighting (`RGB`, `pixelBuf`, effects, layers, drivers, layouts, modifiers) | `modules/lights/` |
| Frontend bundle (gzipped SPA) served on `GET /` | `src/frontend/` |

**The pal/module split is the rule that decides location.** If the file would need `#ifdef ARDUINO` to do its job, it lives in `pal/`. If the file uses the pal interface with no conditionals of its own, it is a module.

The runtime never references any of these. They reference the runtime.

---

## Concurrency model

**Topology: arbitrary DAG. Mechanism: SPSC lock-free ring buffer per edge, depth 2 by default.**

A linear pipeline (effect → blend → driver) is the trivial case of an arbitrary DAG. Per-edge SPSC ring buffers give zero contention by construction (one writer, one reader), no allocation after init, bounded memory known at wire time, and degenerate to classic double-buffering at depth 2 — which is what fits on esp32dev without PSRAM. On ESP32-S3 / PC with memory headroom, depth >2 gives pipelining and backpressure for free. There is no alternative concurrency primitive in the runtime; everything that crosses a core boundary uses this one.

---

## The core files

```
src/core/MoonModule.h       — the contract: lifecycle + controls + identity (≤ 600 LOC)
src/core/ModuleManager.cpp  — add / remove / replace / wire
src/core/Scheduler.cpp      — DAG runner with SPSC rings
src/pal/*.h                 — one file per platform concern (see Pal section)
```

Per-file LOC budgets are enforced by `scripts/check_loc.py`; structural additions in `src/pal/` require an entry in `BUDGETS`. `src/core/MoonModule.h` ≤ 600 LOC; the rest of `src/core/` ≤ 300 LOC; each pal file has its own cap. Verified per release by CI (see [process architecture](process.md)).
