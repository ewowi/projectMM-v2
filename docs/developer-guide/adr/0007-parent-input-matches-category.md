# ADR 0007 — Parent-input match: name == type OR category OR source

**Status:** Accepted, 2026-05-17.
**Supersedes:** none.
**Refines:** the Sprint-17 parent-input model ([system.md "Inputs, and the parent input"](../../architecture/system.md)).
**Related:** [CLAUDE.md](https://github.com/ewowi/projectMM-v2/blob/main/CLAUDE.md) Rule #1 / Lineage ("strcmp on type names" v1 drift); [ModuleManager.cpp](https://github.com/ewowi/projectMM-v2/blob/main/src/core/ModuleManager.cpp) `parent_input_idx_`.

## Context

Sprint 17 made the parent *be* an input: reparenting C onto P sets the
parent flag on C's input whose **name == P's registered factory type**, or
the `"source"` wildcard, else the drop is rejected (system.md).

This produced a self-inconsistency, found while authoring a layout-swap
scenario. Effects (`PixelEffectBase` subclasses) declare exactly one input,
keyed `"layout"`. Layouts register under distinct factory types:
`GridLayoutModule`→`"layout"`, `RingLayoutModule`→`"ring-layout"`,
`WheelLayoutModule`→`"wheel-layout"` — but all three derive `LayoutModule`,
whose virtual `category()` returns `"layout"`.

Result:

- reparent effect → Grid (type `"layout"`): input `"layout"` == type
  `"layout"` → nests. ✓
- reparent effect → Ring/Wheel (type `"ring-layout"`/`"wheel-layout"`): no
  input named that, no `"source"` on effects → **rejected by design**.
  Ring/Wheel layouts are structurally un-nestable by any effect.

Meanwhile **data-flow resolution already matches layouts by category**:
`PixelEffectBase::resolve_layout_()` walks the parent chain / `find()` and
matches `category()=="layout"`, never the type string (the header comment
explicitly names type-string matching "the v1 strcmp-on-type-name
pattern"). So an effect can *resolve* a Ring/Wheel layout's geometry but
cannot be *nested* under it: the two halves of one relationship
(structural nesting vs. data resolution) use two different match rules.
That asymmetry is the defect — not a missing feature.

## Decision

The parent-input match adds **category** as a match key. An input matches
parent P if its name equals:

1. `P.type()` — exact factory type (unchanged; still wins), **or**
2. `P.category()` — the module's domain category, **or**
3. `"source"` — the type-agnostic wildcard (unchanged).

**Precedence: exact type > category > source.** Exact type returning first
preserves the dual-input case: an effect with both a generic `"layout"`
input and a specific `"ring-layout"` input, dropped on a Ring, binds the
specific input. Category is the new middle rung; `"source"` stays last.

`parent_input_idx_(child, parent)` now takes the parent **module** (to call
`category()`), not just its type string. Single call site (`reparent()`)
already holds the parent module. Net ≈ +5 LOC in one core function; no new
file, no new concept — `category()` already exists and is already the match
key on the data-resolution side. This *removes* the type-vs-category split,
deleting the last "strcmp on type names" drift from the reparent path.

## Consequences

- An effect's generic `"layout"` input now nests under any
  `category()=="layout"` module (Grid/Ring/Wheel). Canvas drag of an effect
  onto a Ring/Wheel node, and the layout-swap scenario, now work. The
  reference pipeline is unchanged (ripples→layout binds via exact type;
  preview/artnet→ripples via `"source"`).
- Structural nesting and data resolution now use the **same** category rule
  — the inconsistency is gone.
- **Tradeoff (locked in):** the type string can no longer make one layout
  kind un-nestable. If a future requirement needs "nests under Grid but not
  Ring", that must be a *type*-named input (`"ring-layout"`) — exact-type
  precedence already supports it, so the door stays open, but the absence
  of a category match is no longer the lever.
- `test_reparent.cpp`: existing assertions unchanged (their test parents'
  `type()`/`category()` don't collide); one new case pins category match,
  one pins exact-type-beats-category precedence.

## Why an ADR (not a silent change)

system.md states the match rule explicitly in two load-bearing lines
("name equals P's type … or `source`"). Adding `category()` is a deliberate
amendment to the documented parent-input model — CLAUDE.md: "architecture
changes only via ADR — never silently." It is one model change, recorded
once; the paired system.md edits are surgical (the two match-rule
sentences). No `// PATCH:` — this is a true fix of an asymmetry, not a
workaround.
