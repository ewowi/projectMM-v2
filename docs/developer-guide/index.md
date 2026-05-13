# Developer Guide

How to work in this repo: the build / flash / test path, and the immutable decisions that constrain it.

- [Deploy](deploy.md) — *How do I build, flash, test?* MoonDeck (the interactive dev console with PC / ESP32 / Live / Develop tabs), the per-script reference (build / test / flash / monitor / scenarios / agent tasks), and the direct-shell + pre-commit invocation path that CI uses.
- [Pal inventory](pal.md) — *Which pal files exist and which module uses which?* Per-file LOC budgets, module ↔ pal cross-reference, deferred pal files. The Pal *rule* is contract-level and lives in [system.md](../architecture/system.md#pal-the-only-place-platform-conditionals-appear); this page is the inventory.
- ADRs — *Why was X decided?* Immutable architecture-decision records. A new top-level path or structural change requires one.
    - [0001 — Vendor cpp-httplib](adr/0001-vendor-cpp-httplib.md)
    - [0002 — ArduinoJson via lib_deps](adr/0002-arduinojson-lib-deps.md)

What's been done, what's next, and what's deferred lives one level over in [Development](../development/index.md).

## The contract above this section

[Process architecture](../architecture/process.md) is the contract under which every release executes — minimalism (the main driver), the guardrails that enforce it, and the anti-drift rules that keep the framework itself from eroding. It is not a release artefact but a constraint on every release that follows. Any pull request that bypasses or relaxes it carries a paired ADR file.
