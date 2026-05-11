# System Architecture

The core is four pieces. Everything else is a module or a domain on top of the core.

```
Module                              — the contract
ModuleManager                       — owns Module instances
Scheduler                           — runs the DAG across cores
Pal                                 — platform abstraction, nothing more
```

This page is the constraint. Any pull request that does not fit the picture below is either rejected or carries a paired ADR file (`docs/adr/NNNN-*.md`) recording the architecture change explicitly. See [process architecture](process.md) for why.

---

## Module — the contract

```cpp
class Module {
public:
  virtual void setup()      {}     // allocate, configure, wire
  virtual void loop()       {}     // hot path: bounded time, no alloc, no block
  virtual void loop20ms()   {}     // sub-hot responsiveness: periodic, lower urgency
  virtual void loop1s()     {}     // monitoring, health
  virtual void loop10s()    {}     // housekeeping, persistence
  virtual void teardown()   {}     // free every setup() allocation
};
```

The contract is the entire module-facing API of the runtime. Six virtuals, all optional, all empty by default. A module that only overrides `setup()` and `loop()` pays for nothing it does not use.

**Hot path.** `loop()` is the load-bearing decision in the runtime: it must do as little as possible at the maximum frequency the platform allows. The guardrails enforce this mechanically — no allocations, no blocking calls, no logging in any `loop*()` body — see [process architecture](process.md).

**Tiered cadences.** `loop20ms`, `loop1s`, and `loop10s` exist to drain less urgent work out of the hot path at progressively lower rates so `loop()` stays short. A module decides for itself which cadences it needs; the scheduler pays nothing for cadences a module does not override.

**Multi-core.** The runtime is built to exploit every core the platform offers. Several `loop()` instances run in parallel, connected as a DAG. The scheduler pins a separate task per core and arranges the topology declared at wire time.

---

## ModuleManager — instance ownership

```cpp
class ModuleManager {
public:
  Module* add(const char* type, const char* id);
  bool    remove(const char* id);
  bool    replace(const char* id, const char* newType);
  void    wire(const char* fromId, const char* toId);
};
```

ModuleManager owns Module instances (`std::unique_ptr`). It exposes structural operations only: add, remove, replace, wire. **It does not** parse REST requests, serialise to JSON, write state files, debounce dirty flags, or accumulate memory accounting. Those are jobs of separate modules (`HttpModule`, `StateModule`, etc.) that depend on the ModuleManager, not the other way around.

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

## Pal — platform abstraction, nothing more

```
pal::millis()             pal::sem_create()
pal::micros()             pal::sem_take(timeout)
pal::yield()              pal::sem_give()
pal::sleep(ms)
                          pal::fs_read(path)
pal::gpio_init(pin, mode) pal::fs_write(path, bytes)
pal::gpio_write(pin, v)   pal::fs_exists(path)
pal::gpio_read(pin)
                          pal::free_heap_bytes()
pal::task_pin(fn, core)   pal::max_alloc_bytes()
```

**That is all of it.** Networking (WiFi, Ethernet, UDP, TCP, mDNS), OTA, NTP, HTTP servers, WebSocket servers, REST APIs, file-format parsers, JSON, and all system-info dumps (flash size, OTA partition layout, reset reasons, CPU frequency) **are not** in Pal. They are modules that depend on Pal.

The line that separates platform abstraction from infrastructure-built-on-platform is: **could this be tested with a fake adapter on PC for the purpose it exists?** Timing, GPIO, filesystem, RTOS primitives, heap — yes. WiFi association handshake — no, that is its own module with its own tests.

`Pal` total target: ≤ 200 LOC across all platforms.

---

## What is not in the core

| Domain | Belongs to |
|--------|------------|
| Networking (WiFi, Ethernet, mDNS) | `modules/network/` |
| HTTP server, WebSocket, REST API | `modules/network/` |
| OTA firmware updates | `modules/firmware/` |
| NTP wall-clock sync | `modules/system/` |
| State persistence (LittleFS, JSON files) | `modules/state/` |
| Lighting (`RGB`, `pixelBuf`, effects, layers, drivers, layouts, modifiers) | `modules/lights/` |

The runtime never references any of these. They reference the runtime.

---

## Concurrency model

**Topology: arbitrary DAG. Mechanism: SPSC lock-free ring buffer per edge, depth 2 by default.**

A linear pipeline (effect → blend → driver) is the trivial case of an arbitrary DAG. Per-edge SPSC ring buffers give zero contention by construction (one writer, one reader), no allocation after init, bounded memory known at wire time, and degenerate to classic double-buffering at depth 2 — which is what fits on esp32dev without PSRAM. On ESP32-S3 / PC with memory headroom, depth >2 gives pipelining and backpressure for free. There is no alternative concurrency primitive in the runtime; everything that crosses a core boundary uses this one.

---

## The four files

```
src/core/Module.h           — the 6-method contract (header only)
src/core/ModuleManager.cpp  — add / remove / replace / wire (≤ 200 LOC)
src/core/Scheduler.cpp      — DAG runner with SPSC rings (≤ 300 LOC)
src/pal/Pal.h               — the platform abstraction (≤ 200 LOC)
```

Total core target: ≤ 300 LOC excluding Pal. Verified per release by CI (see [process architecture](process.md)).
