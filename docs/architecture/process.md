# Process Architecture

How projectMM v2 is built and changed. This page is a constraint on every release. A pull request that violates it either is rejected or carries a paired ADR file (`docs/adr/NNNN-*.md`) recording the change explicitly.

The [system architecture](system.md) constrains the artifact. This page constrains how the artifact changes.

---

## 1. Frugality — the main driver

**Frugal in code, in CPU cycles, and in resources.** Every change answers two questions before it lands:

1. Does this addition pay for itself? A new file, test surface, doc page, or deploy script — what does it replace or extend?
2. What did you remove? Every release removes at least one thing. A release that adds without removing must justify why.

If the answer to (1) is "nothing", the PR is rejected.

The three dimensions are tested differently:

- **Code frugality** — whether a reader can hold the core in their head. Measured in LOC budgets per surface and in reviewer cognitive load.
- **CPU frugality** — `loop()` time under realistic load on every active core.
- **Resource frugality** — static and runtime footprint per module across RAM, flash, and any other constrained resource.

Frugality applies equally to source, tests, deploy scripts, and documentation. The principle is not new: essential complexity only (Brooks), Gall's Law, and the Saint-Exupéry rule that perfection is reached not when nothing more can be added but when nothing more can be taken away.

---

## 2. Guardrails — frugality enforced mechanically

Frugality is a property that erodes silently. Guardrails make the erosion mechanical to detect. They land in Sprint 1 of Release 1, before any module exists; no commit in v2 is ever written without them running against it.

Three enforcement tiers, in order of when they fire:

- **Pre-commit** — fires before the diff is finished. Catches violations of the hot-path rules (no allocations, no blocking calls, no logging in any `loop*()` body) and architectural fitness functions (pin numbers trace to typed board config — `pinMode(5, ...)` is rejected, `pinMode(BoardPins::WS2812_DATA, ...)` is the correct form).
- **CI gates** — block PR merge. Enforce LOC budgets per surface, footprint baselines per module, test-count baselines, and a doc-growth budget per release. Overshoot fails; a bump requires an explicit signed-off line in the release plan.
- **Structural additions** — block additions that bypass the design. A new top-level directory requires an ADR. A new file under `deploy/`, `tests/`, or `docs/` requires an inline justification of what existing surface was insufficient.

The specific tools that implement each tier (linters, formatters, static analysers, hook scripts) are an implementation detail of Sprint 1, not a constraint. They earn their place by paying for themselves under the same frugality rule they enforce.

---

## 3. Anti-drift — why these rules survive

The guardrails above describe *what* is enforced. Anti-drift describes *why the framework itself does not erode* — the lesson from v1, where the verifier always trailed the code it was meant to constrain.

- **Guardrails ahead, never behind.** The framework ships before the code it constrains. This inverts the v1 timeline.
- **Mandatory subtraction.** Every release removes at least one thing. "What did you remove?" is a sprint-close question alongside "what did you add?". A system that never removes anything will always grow until someone considers a restart.
- **Frugality is a review gate.** Every PR answers the two questions in §1 before any other.
- **Fix causes, not symptoms.** When a change does not behave correctly, find why and fix it at the cause. Do not add code that masks a symptom — every such patch creates a new failure mode that needs its own patch, and the system grows by accretion. Patterns to refuse: the retry added because something is flaky, the timer added because something races, the try/catch that swallows an error "just in case", the re-init that papers over inconsistent state. If the cause cannot be identified, the work is blocked — not a chance to add a band-aid.
- **Architecture is a constraint, not a description.** [System architecture](system.md) is the reference for every review. Changes to it happen only via ADR — never silently. v1's drift came from the architecture quietly becoming one document among many.
- **Recurring evaluation releases.** Every fifth release is a no-feature evaluation that walks the code, checks for drift, and proposes removals. The schedule is set at the start of v2 Sprint 1, not at the end of Release 4.
- **Drift metrics in CI.** Per-release deltas published as a status doc: file count, LOC per surface, ratio of new tests to new code, modules added vs removed. Any metric trending the wrong way across three consecutive releases is flagged in the next retrospective.
- **The verifier obeys the same rules it enforces.** Adding a new pre-commit hook, CI gate, or analysis script requires the same justification as adding a new module: what it replaces, what it extends. Without this rule the guardrails framework drifts the way `deploy/` drifted in v1.

None of these is sufficient on its own. Together they create the friction that prevents drift from accumulating below the threshold where anyone notices it.
