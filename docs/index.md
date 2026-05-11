# projectMM v2

A cross-platform runtime for modular, loop-driven processes.

A `Module` has a six-method lifecycle: `setup()`, `loop()` (hot path), `loop20ms()`, `loop1s()`, `loop10s()`, `teardown()`. The `Scheduler` runs modules as a directed acyclic graph of stages, pinned across every core the platform offers, with lock-free SPSC ring buffers carrying data across core boundaries. The `Pal` exposes only what is truly platform-specific — timing, GPIO, filesystem basics, RTOS primitives, heap query — and nothing else.

The entry domain is **light control**: LEDs over GPIO or network. Effects, layers, drivers, layouts, and modifiers all live in `modules/lights/` as a domain on top of the runtime. The runtime itself has no knowledge of pixels, frames, or LEDs.

## The main driver

**Frugality** — frugal in code, in CPU cycles, and in resources. The three dimensions are tested differently:

- **Code frugality**: whether a reader can hold the core in their head.
- **CPU frugality**: `loop()` time under realistic load on every active core.
- **Resource frugality**: static and runtime footprint per module across RAM, flash, and any other constrained resource.

Frugality applies equally to source, tests, deploy scripts, and documentation. The principle is not new: essential complexity only (Brooks), Gall's Law, and the Saint-Exupéry rule that perfection is reached not when nothing more can be added but when nothing more can be taken away.

The mechanisms that keep frugality from eroding are documented in [anti-drift rules](development/anti-drift.md) and enforced by the [guardrails framework](development/guardrails.md). Both ship in Sprint 1 of Release 1, before any module is written.

## Where to go next

- [Architecture](architecture.md) — the core in one page.
- [Lights](lights.md) — the first domain (filled in during Release 1 Sprint 4).
- [Deploy](deploy.md) — build, flash, test (filled in during Release 1 Sprint 1).
- [Release 1 plan](development/release-01.md) — six sprints to parity with v1.
