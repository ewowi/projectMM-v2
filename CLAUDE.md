# CLAUDE.md

Guidance for Claude Code (claude.ai/code) and other AI agents working on projectMM v2.

## Rule #1: Minimalism

Minimal in code, in CPU cycles, and in resources. This is the load-bearing constraint of the project. Every change answers two questions before it lands:

1. **Does this addition pay for itself?** A new file, a new test surface, a new doc page, a new deploy script — what does it replace or extend?
2. **What did you remove?** Every release removes at least one thing. A PR that adds without removing must justify why.

If the answer to (1) is "nothing", the PR is rejected. See [docs/architecture/process.md](docs/architecture/process.md) for why this matters and how it is enforced mechanically.

## Docs as agent memory

Three layers of project memory live in `docs/`. Read them with their lifespan in mind:

- **Long-term memory — [architecture/](docs/architecture/).** [system.md](docs/architecture/system.md) (the four core pieces + the pal/module split) and [process.md](docs/architecture/process.md) (minimalism, guardrails, anti-drift, port-and-minimize). These change only via ADR. Treat them as the constitution: every PR must fit them, and if you find a conflict the architecture wins unless an ADR explicitly amends it.
- **Way of working — [developer-guide/](docs/developer-guide/).** [deploy.md](docs/developer-guide/deploy.md) (MoonDeck + scripts), [pal.md](docs/developer-guide/pal.md) (the pal-file inventory), and the [ADRs](docs/developer-guide/adr/). This is *how* to work in the repo: build/flash/test, which pal each module uses, and the immutable architecture decisions. Stable; updated when the way of working itself changes.
- **Short-term memory — [development/](docs/development/).** The ongoing release ([release-01.md](docs/development/release-01.md)) and the [backlog](docs/development/backlog.md). This is what's been shipped, what's next, and what's deferred. Churns release-by-release. Read it for context on the current sprint; do not treat its contents as durable contract — anything that needs to outlive the release must be promoted into architecture or developer-guide via the usual process.

When you read a memory and act on it, prefer the longer-lived layer. If something in `development/` conflicts with `architecture/`, architecture is right and the release doc is stale — fix it.

## The architecture is a constraint

[docs/architecture/system.md](docs/architecture/system.md) is short, durable, and the reference for every review. A change that does not fit the picture there is either rejected or carries a paired ADR file (`docs/developer-guide/adr/NNNN-*.md`) recording the architecture change explicitly. The architecture changes only via ADR — never silently.

The core boundary:

- **In core:** `Module`, `ModuleManager`, `Scheduler`, `Pal` (timing / GPIO / fs / rtos primitives / heap query — nothing more).
- **Not in core:** networking (WiFi, Ethernet, UDP, mDNS), HTTP / WebSocket / REST, OTA, NTP, state persistence, lighting (`RGB`, `pixelBuf`, effects, layers, drivers, layouts, modifiers).

If you find yourself adding networking or system-info queries to `Pal`, you are doing the v1 thing again. Stop and write the ADR.

## Hot path rules

`loop()` is the load-bearing decision in the runtime. Inside any `loop()`, `loop20ms()`, `loop1s()`, or `loop10s()`:

- **No allocations.** No `new`, `malloc`, `psram_malloc`, `JsonDocument` construction. Allocate in `setup()`, in `onUpdate()` when a control changes, or in `teardown()`. Reallocation on reconfiguration (panel resize, module add) is fine because it runs outside `loop`.
- **No blocking.** No `delay`, `vTaskDelay`, `sleep`, `usleep`, `recv` with positive timeout.
- **No logging.** Info-level logging inflates measured timing. Use `loop1s` for periodic logging.

These are enforced by pre-commit hooks, not by review. The hook script is the source of truth for what counts as a violation.

## GPIO

Pin numbers never appear as integer literals in module code. Pins come from a typed board configuration (`board/<env>.yaml` → `Pins.hpp` codegen). Writing `pinMode(5, OUTPUT)` is blocked by pre-commit; `pinMode(BoardPins::WS2812_DATA, OUTPUT)` is the correct form. Dynamic pin assignment (a module picking its pin from a config struct loaded at runtime) is fully supported — the rule is about traceability of the pin number, not whether it is static.

## Guardrails obey the same rules they enforce

Adding a new pre-commit hook, CI gate, or analysis script requires the same structural justification as adding a new module. What does it replace? What does it extend? Without this rule the guardrails framework drifts the way `deploy/` drifted in v1.

## Lineage

v2 is the successor to [projectMM v1](https://github.com/ewowi/projectMM). v1's drift to the point of restart is documented in [v1's Release 9](https://ewowi.github.io/projectMM/development/release-09/). When you find yourself doing something that feels familiar from v1 — adding a fourth test surface, growing `Pal.h` with another networking function, sprinkling `strcmp` on type names in `ModuleManager` — stop and check whether v1 made that mistake first.
