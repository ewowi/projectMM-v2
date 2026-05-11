# projectMM v2

A cross-platform runtime for modular, loop-driven processes.

A `Module` has a six-method lifecycle: `setup()`, `loop()` (hot path), `loop20ms()`, `loop1s()`, `loop10s()`, `teardown()`. The `Scheduler` runs modules as a directed acyclic graph of stages, pinned across every core the platform offers, with lock-free SPSC ring buffers carrying data across core boundaries. The `Pal` exposes only what is truly platform-specific — timing, GPIO, filesystem basics, RTOS primitives, heap query — and nothing else.

The entry domain is **light control**: LEDs over GPIO or network. Effects, layers, drivers, layouts, and modifiers all live in `modules/lights/` as a domain on top of the runtime. The runtime itself has no knowledge of pixels, frames, or LEDs.

## The main driver

**Frugality** — frugal in code, in CPU cycles, and in resources. The full statement of the principle, the guardrails that enforce it, and the anti-drift rules that keep the framework itself from eroding live in [process architecture](architecture/process.md). Both the system and process architectures ship as the contract under which every sprint executes.

## Where to go next

- [System architecture](architecture/system.md) — the core in one page.
- [Process architecture](architecture/process.md) — frugality, guardrails, anti-drift.
- [Lights](lights.md) — the first domain (filled in during Release 1 Sprint 4).
- [Deploy](deploy.md) — build, flash, test (filled in during Release 1 Sprint 1).
- [Release 1 plan](development/release-01.md) — six sprints to parity with v1.
