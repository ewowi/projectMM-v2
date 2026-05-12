# ADR 0002 — Use `lib_deps` for ArduinoJson; codify the registry-vs-vendor rule

**Status:** Accepted, 2026-05-12.
**Supersedes:** none. **Refines:** [ADR 0001](0001-vendor-cpp-httplib.md) by codifying the rule it implicitly applied.
**Related:** [Release 1, Sprint 3](../development/release-01.md#sprint-3) — `StatefulModule` port needs JSON.

## Context

Sprint 3's port of v1's `StatefulModule.h` brings in JSON-driven control schemas, value setting, and state persistence. v1 uses `bblanchon/ArduinoJson @ ^7` everywhere (`JsonDocument`, `JsonVariantConst`, `JsonObject`, `JsonObjectConst`, `JsonArray`). Replacing it with a hand-rolled JSON shim would force a rewrite of every ported module against the new shim — pushing us back toward the greenfield trap that triggered the Sprint 2 reset.

v2 therefore needs ArduinoJson. The question is **how** to get it.

ADR 0001 vendored cpp-httplib at `lib/httplib/`. That decision was driven by a specific gap: `cpp-httplib` is **not in PlatformIO's library registry**. The only alternative there was `lib_deps` pointing at the upstream GitHub URL — which would have made PlatformIO's library scanner try to compile cpp-httplib's demos and tests. Vendoring was the cleanest option *because the registry path wasn't available*, not because vendoring is the better default.

ArduinoJson is **in PIO's registry**. The `lib_deps` path that was unworkable for cpp-httplib is the natural path here: `bblanchon/ArduinoJson @ ^7.2.1` — the same line v1 uses.

## Decision

**Add ArduinoJson via `lib_deps` in `platformio.ini`:**

```ini
lib_deps =
    bblanchon/ArduinoJson @ ^7.2.1
```

**Codify the rule for future third-party dependencies:**

> When a third-party library is **in PlatformIO's registry**, add it via `lib_deps` with a version constraint. When it is **not in the registry**, vendor it under `lib/<name>/` per ADR 0001.

This rule explains both ADRs together: cpp-httplib gets vendored because it must; ArduinoJson gets `lib_deps` because it can. Both decisions still require their own ADR — the rule decides the *mechanism* but the ADR records the *dependency*.

## Consequences

**Cost.**
- Build needs network access on a cold PlatformIO cache (first build of a fresh checkout). CI caches `.pio/libdeps/` between runs, so this is a one-time cost per cache eviction.
- The dependency is not visible in the source tree — readers have to consult `platformio.ini` to see what we depend on.
- A version constraint (`@ ^7.2.1`) accepts patch and minor updates automatically. If upstream introduces a regression we'd inherit it; mitigate by pinning to `@ 7.2.1` exact if it ever bites.

**Benefit.**
- PlatformIO devs recognize `lib_deps` immediately — no `lib/` directory hunt, no "why is there 6k LOC of vendored code in our repo".
- Matches v1's exact dependency declaration: `bblanchon/ArduinoJson @ ^7` (v2 pins to `^7.2.1` for an explicit floor).
- Updates are a one-line bump.
- Build flag `-DARDUINOJSON_ENABLE_*` macros (if needed for footprint tuning on ESP32) live in `platformio.ini` next to the dependency itself.

**What is *not* allowed under this ADR.**
- Adding any other third-party library without an ADR. The rule above tells you which bucket the new ADR fits — it does not authorize silent additions.
- Mixing both for the same library (do not also vendor ArduinoJson). One source of truth per dep.

## Future deps cheat sheet

| Library | In PIO registry? | Mechanism | ADR |
|---|---|---|---|
| cpp-httplib | No | Vendored at `lib/httplib/` | 0001 |
| ArduinoJson | Yes | `lib_deps` | 0002 (this) |
| ESPAsyncWebServer | Yes | `lib_deps` (Sprint 4) | TBD |
