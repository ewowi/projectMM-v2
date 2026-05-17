# Product Requirements

What projectMM v2 must do for the end user. This is the third constraint
alongside [system](system.md) and [process](process.md): the others say
*how the code is shaped*; this says *what it must deliver*. A capacity floor
on a no-PSRAM device is a Rule #1 resource-minimalism input — the same kind
of binding number as a LOC budget. Changing a **binding** target is an
architecture change and carries a paired ADR — never silent. See
[ADR 0008](../developer-guide/adr/0008-product-requirements-constraint.md).

There is no product-owner agent and no end-user agent. The product intent
lives in *this document*; the existing `architect` and `minimalism-reviewer`
enforce it. (Authority chain: the documents are the contract; agents are
advisors that produce findings, not rulings; the maintainer ratifies any
target change via an ADR. See ADR 0008.)

---

## Two tiers

- **Binding** — a hard guarantee. A change that regresses a binding target,
  or a design that cannot meet it, is a **constitutional conflict**: it is
  rejected or carries an ADR amending the target. `minimalism-reviewer`
  treats a binding regression at the same bar as an unjustified LOC-budget
  bump; `architect` treats "this design cannot meet a binding floor" as a
  conflict, not a tradeoff.
- **Aspirational** — a stretch we pursue when minimalism allows. Missing an
  aspirational target is an **informational note, never a blocker** and
  never an ADR trigger. It must not silently become a de-facto floor.

---

## Capacity (LED count)

| Target | Tier | Notes |
|---|---|---|
| ≥ 4096 LEDs on a no-PSRAM ESP32 (`esp32dev`) | **Binding** | The reference floor. Drives per-module RAM footprint; a module that pushes the default pipeline below this on `esp32dev` is over budget. |
| Max practical LED count on PSRAM devices (`esp32s3_n16r8`, large setups) | **Binding** | "As many as the 8 MB PSRAM holds for a sensible pipeline." Not a fixed number — the binding part is *PSRAM is used by default when present and large setups are first-class on S3*. |
| ≥ 12288 LEDs on a no-PSRAM ESP32, minimal config | **Aspirational** | Stretch. Pursued only if it falls out of minimalism work; never traded *for* by adding complexity, and never blocks a change for missing it. |

## Throughput

| Target | Tier | Notes |
|---|---|---|
| Highest practical processing-pipeline FPS (lights domain is the worked example) | **Binding** | The hot-path rules in [process](process.md) exist to protect this. "Regression" is measured against the **reference setup** (`reference-setup` scenario) FPS recorded by the latest HIL run / scenario `measure` baseline — not a subjective judgement. A change that drops that figure without an ADR is a finding; a change with no measured baseline yet cannot be a finding (record the baseline first). |
| ESP32-P4 as the speed ceiling | **Binding** | When a P4 target exists, it is *the* max-speed reference; the pipeline must scale to it (not be artificially capped for convenience on slower parts). |

## User experience

| Target | Tier | Notes |
|---|---|---|
| Desktop-optimized, mobile-workable UI | **Binding** | Desktop is the primary surface and must be fully usable; mobile must be *workable* (not necessarily optimal). A desktop-regressing change is a finding. |
| Industry-standard interaction patterns | **Binding** | Follow recognised prior art (node/canvas editors in the spirit of TouchDesigner / MADRIX) rather than inventing exotic UX. "A competent user of those tools recognises this" is the bar. |

## Transparency

| Target | Tier | Notes |
|---|---|---|
| The user-facing docs surface *what has been tested* | **Binding** | The generated [test list](../developer-guide/tests.md) and HIL records are the source; the user guide must link them so users see the tested surface. Reuses existing artefacts — adds no new mechanism. |

---

Per-module behaviour and the how-to live in the [User Guide](../user-guide/index.md)
and [Developer Guide](../developer-guide/index.md). This page is the contract
for *what the product owes the user*; those are the catalogue and the manual.
