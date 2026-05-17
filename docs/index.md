# projectMM v2

A cross-platform runtime for modular, loop-driven processes.

A `Module` has a six-method lifecycle: `setup()`, `loop()` (hot path), `loop20ms()`, `loop1s()`, `loop10s()`, `teardown()`. The `Scheduler` runs modules as a directed acyclic graph of stages, pinned across every core the platform offers, with lock-free SPSC ring buffers carrying data across core boundaries. The `Pal` exposes only what is truly platform-specific — timing, GPIO, filesystem basics, RTOS primitives, heap query — and nothing else.

The entry domain is **light control**: LEDs over GPIO or network. Effects, layers, drivers, layouts, and modifiers all live in `modules/lights/` as a domain on top of the runtime. The runtime itself has no knowledge of pixels, frames, or LEDs.

## The main driver

**Minimalism** — minimal in code, in CPU cycles, and in resources. The full statement of the principle, the guardrails that enforce it, and the anti-drift rules that keep the framework itself from eroding live in [process architecture](architecture/process.md). Both the system and process architectures ship as the contract under which every sprint executes.

## Where to go next

Four top-level sections; each has an Overview page that lists what's under it.

- **[User Guide](user-guide/index.md)** — per-module reference (controls + lifecycle overrides), grouped by category (system / network / lights).
- **[Architecture](architecture/index.md)** — the three contracts: [System](architecture/system.md) (the four core pieces), [Process](architecture/process.md) (minimalism, guardrails, anti-drift, port-and-minimize), and [Product](architecture/product.md) (binding capacity/throughput floors + the desktop-first UX standard).
- **[Developer Guide](developer-guide/index.md)** — how to build, flash, and test ([Deploy](developer-guide/deploy.md)), plus the [ADRs](developer-guide/adr/0001-vendor-cpp-httplib.md) that constrain it.
- **[Development](development/index.md)** — what's shipped ([Release 1](development/release-01.md)), what's next + deferred ([Backlog](development/backlog.md)).
