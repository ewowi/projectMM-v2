# Architecture

Two contracts under which projectMM v2 is built and changed. Every pull request must fit both; anything that doesn't carries a paired [ADR](../developer-guide/adr/0001-vendor-cpp-httplib.md). Architecture changes only via ADR — never silently.

- [System architecture](system.md) — the four core pieces (`MoonModule`, `ModuleManager`, `Scheduler`, `Pal`) in one page. What lives in core and what doesn't (networking, lighting, persistence are all modules, not core). The MoonModule contract — lifecycle + setup-time hooks (`onBuildControls`, `onAllocateMemory`, `onUpdate`) + control system — read by anyone writing or extending a module.
- [Process architecture](process.md) — the four rules under which the project evolves: **Minimalism** (the main driver), **Guardrails** (enforced mechanically by `scripts/check_*.py` + pre-commit + CI), **Anti-drift** (the framework itself does not erode), and **Port-and-minimize** (where substantive modules come from). Read this when proposing structural changes or new files.

Per-module specifics — controls, lifecycle overrides — live in the [User Guide](../user-guide/index.md), not here. This section is the contract; the User Guide is the catalogue.
