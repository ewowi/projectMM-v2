# ADR 0006 — Per-effect / per-layout LOC budgets

**Status:** Accepted, 2026-05-17.
**Supersedes:** none.
**Related:** [process architecture §2 — guardrails obey their own rules](../../architecture/process.md); [CLAUDE.md](https://github.com/ewowi/projectMM-v2/blob/main/CLAUDE.md) Rule #1; [check_loc.py](https://github.com/ewowi/projectMM-v2/blob/main/scripts/checks/check_loc.py).

## Context

`src/modules/lights/` had a single aggregate LOC budget (`"src/modules/lights": 600`).
The lights domain is the one surface designed to grow indefinitely: every new
visual effect and every new panel layout is a new file there. A single
aggregate ceiling has two failure modes against Rule #1:

1. **It hides per-effect bloat.** A 400-line effect and a 40-line effect are
   indistinguishable under one total; the ceiling can be satisfied while an
   individual effect is doing far too much.
2. **It becomes a ratchet.** As effects accumulate the number is bumped
   600 → 1400 → 2200 → … . Each bump is "true" yet the bumps carry no
   per-effect deliberation — exactly the unbounded-expansion pattern the
   minimalism rule exists to prevent. Projected to 10 000+ LOC the aggregate
   number means nothing.

The codebase already solved this exact problem for `src/pal/`: **one budget
per file**, and a gate (`check_pal_files_have_budgets`) that fails if any pal
file lacks an entry — "adding a new pal concern is a moment of deliberation,
not a free expansion." Effects and layouts are the same shape of growth.

This change also lands alongside the v1 effect/layout port (5 effects + 2
layouts) and a `PixelEffectBase` / `LayoutModule` extraction that removed the
~6× duplicated resolve/allocate/teardown boilerplate the ports first carried.

Alternatives considered:

- **Keep one aggregate, bump 600 → 1400.** Rejected: defers the ratchet one
  step; the number stops being a discipline signal as the surface grows.
- **Drop the lights budget entirely.** Rejected: removes the only mechanical
  brake on the surface most prone to drift — the opposite of Rule #1.
- **Per-directory sub-budgets (`effects/`, `layouts/`).** Rejected: same
  hiding problem one level down; a bloated single effect is still masked by
  lean siblings in the same directory.

## Decision

Effects and layouts are **per-file budgeted**, mirroring `src/pal/`:

- `scripts/checks/check_loc.py` `BUDGETS` gains one entry per file under
  `src/modules/lights/effects/` and `src/modules/lights/layouts/`, each with a
  one-line comment naming the effect/layout.
- A new gate `check_lights_files_have_budgets()` fails the check if any
  `.h/.cpp` under those two directories has no `BUDGETS` entry. Adding an
  effect or layout therefore *requires* adding its budget line — the
  deliberation moment.
- `"src/modules/lights"` stays as a (smaller) aggregate for the shared
  non-effect/non-layout files only (`RGB.h`, `Pixelable.h`, `PreviewModule.h`,
  `ArtnetOutModule.h`, `GridLayoutModule.h`). `check_loc.py` is already
  nested-aware: per-file entries are excluded from the directory total, so
  there is no double-counting.
- `RipplesEffect.h` is on `PixelEffectBase` like every other effect and so is
  **per-file budgeted by name**, even though it sits in `lights/` root rather
  than `effects/` (historical placement; moving the file is a cosmetic change
  deliberately not bundled here). "Every effect is per-file budgeted" is the
  invariant; directory is incidental. The gate (below) enforces it
  mechanically for the `effects/`+`layouts/` dirs; root-dir effects are
  enumerated explicitly.

`check_loc.py`'s own script budget is bumped (170 → 210) with a justification
comment for the new gate + entries — the guardrail obeying its own rule.

## Consequences

**Cost.**
- `BUDGETS` grows by one entry per effect/layout (≈10 lines now; +1 line per
  future effect). This is intentional: the line *is* the deliberation record.
- A new effect PR must touch `check_loc.py` (add its budget). Same friction
  `src/pal/` already imposes; accepted there, accepted here.
- `check_loc.py` grew ~20 LOC (the gate + entries); budget bumped with reason.

**Benefit.**
- A single bloated effect is now visible (its own line goes OVER) instead of
  hidden under an aggregate.
- No more aggregate ratchet: the lights surface can grow to many effects
  without the budget number degrading into noise.
- Consistent with the established, accepted `src/pal/` discipline — one
  pattern for "surfaces that grow one concern at a time", not two.

**What is *not* changed.**
- The minimalism rule itself; this strengthens its mechanical enforcement.
- Other aggregate budgets (`src/core`, `src/modules/network`,
  `src/modules/system`) — those are bounded surfaces, not per-concern growth,
  and stay aggregate.
- Adding a *new kind* of lights sub-surface (e.g. `drivers/`, `modifiers/`)
  silently: extend `check_lights_files_have_budgets()`'s directory list and
  note it here or in a follow-up ADR — same bar as a new pal concern.
