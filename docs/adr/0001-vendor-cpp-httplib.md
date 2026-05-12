# ADR 0001 — Vendor `cpp-httplib` in `lib/httplib/`

**Status:** Accepted, 2026-05-12.
**Supersedes:** none.
**Related:** [process architecture §4 — port-and-frugalize](../architecture/process.md#4-port-and-frugalize--where-substantive-modules-come-from); [Release 1, Sprint 2](../development/release-01.md#sprint-2).

## Context

Sprint 2 of Release 1 ports v1's `src/core/HttpServer.h` into v2. That file is a thin wrapper over `cpp-httplib` on PC and over `ESPAsyncWebServer` on ESP32 — it does not hand-roll HTTP itself. The PC build therefore needs `httplib.h`.

The first attempt at Sprint 2 hand-rolled an HTTP server in `src/modules/network/HttpServerModule.cpp` and re-discovered classes of bugs `cpp-httplib` had already debugged (TCP fragmentation, body parsing, concurrent connection handling). The frugality principle was being applied to a blank page; it should have been applied to v1's working code. That attempt was deleted; this ADR records how Sprint 2 (redone) sources the dependency.

v1 obtains `cpp-httplib` v0.18.5 via CMake's `FetchContent`. v2 uses PlatformIO exclusively (per Sprint 1's "one build tool" decision) and `cpp-httplib` is not in PlatformIO's registry, so a registry-based `lib_deps` install is not available.

Alternatives considered:

- **PlatformIO `lib_deps` with the upstream GitHub URL.** Avoids a new top-level path. But `cpp-httplib`'s repo ships demos, tests, and CMake-only build files that PlatformIO's library scanner attempts to compile; making PlatformIO use only `httplib.h` requires per-env `build_src_filter` tricks, and the dependency disappears from the source tree, becoming a build-time fetch that breaks offline / cache-cold CI.
- **Revert to CMake for PC + PlatformIO for ESP32.** Matches v1 directly. Doubles the build-system surface area, contradicts Sprint 1's single-build-tool decision, and would force `scripts/build.py` and `scripts/ui.py` to know about both pipelines.

## Decision

Vendor `cpp-httplib` v0.18.5 as a single header at `lib/httplib/src/httplib.h`, alongside the upstream LICENSE at `lib/httplib/LICENSE`. PlatformIO's standard `lib/<name>/src/` convention adds the path to the include search automatically — no `platformio.ini` change needed beyond adding `lib` to the structural allowlist.

`lib` is added to `scripts/check_structure.py`'s allowlist with this ADR as its justification. The directory is reserved for vendored third-party dependencies — not for v2-authored code, which lives in `src/`.

## Consequences

**Cost.**
- ~9k LOC of vendored code lives in the repo. It is not counted by `scripts/check_loc.py` (which inspects only `src/`) and is not maintained by us; updates to `cpp-httplib` require re-vendoring the new release with a follow-up ADR if anything material changes about the dependency.
- A new top-level path (`lib/`).

**Benefit.**
- Build is reproducible and offline-capable: no network at build time.
- The dependency is visible in the tree — readers see what we depend on without consulting build files.
- Matches PlatformIO's standard convention; no platformio.ini extension required.
- Same library, same version as v1 — so behaviour parity is automatic.

**What is *not* allowed under this ADR.**
- Editing the vendored header. Any patch to `cpp-httplib` requires either a new ADR (justifying the fork) or, preferably, an upstream PR and a version bump. v2-side patches to vendored code are exactly the drift this project exists to avoid.
- Adding more files under `lib/httplib/`. The directory holds `src/httplib.h`, `LICENSE`, and nothing else.
- Using `lib/` for v2-authored code. v2 source lives in `src/`; `lib/` is reserved for third-party.
