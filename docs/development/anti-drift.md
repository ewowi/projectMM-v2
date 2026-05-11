# Anti-drift rules

> **Source:** ported verbatim from v1's [Release 9 § What stops this from happening again](https://ewowi.github.io/projectMM/development/release-09/#what-stops-this-from-happening-again). This document is a constraint on every release of projectMM v2 and is not a release artefact. Changes to these rules require a paired ADR.

---

A restart is only valuable if the conditions that caused v1's drift do not reproduce in v2. The mechanisms below are the answer; they ship as part of v2's first sprint, and they are themselves constrained by the rules they enforce.

**Guardrails ahead, never behind.** v2 Sprint 1 lands the guardrails framework before any module exists. No commit in v2 is ever written without pre-commit hooks, CI gates, and structural rules running against it. This inverts the v1 timeline, where the verifier always trailed the code it was meant to constrain.

**Growth budget per release.** Each release plan declares a LOC budget for `src/`, `tests/`, `docs/`, and `deploy/`. CI fails on overshoot. Bumping the budget requires an explicit line in the release plan signed off by the maintainer; it cannot be done silently inside a sprint PR.

**Mandatory subtraction.** Every release removes at least one thing — a deprecated path, an unused module, an obsolete doc page. "What did you remove?" is a sprint-close question alongside "what did you add?". A release with zero removals is allowed only when every existing thing has been re-justified in the retrospective. This is the single rule that matters most: a system that never removes anything will always grow until someone considers a restart.

**Frugality is a review gate.** Every PR answers one question before any other: does this addition pay for itself, and what does it replace or extend? If the answer is "nothing", the PR is rejected. The rule applies equally to tests, docs, and deploy scripts.

**Architecture is a constraint, not a description.** The architecture page is short, durable, and the reference for every review. A PR that does not fit the architecture either is rejected or carries a paired ADR file (`docs/adr/NNNN-*.md`) recording the architecture change explicitly. v1's drift came from the architecture quietly becoming one document among many; v2 makes any architecture change visible by requiring the ADR.

**Recurring evaluation sprints.** v1's Release 9 was the first instance of a recurring pattern, not a one-time event. Every fifth release of v2 is a no-feature evaluation that walks the code, checks for drift, and proposes removals. The schedule is set at the start of v2 Sprint 1, not at the end of Release 4.

**Drift metrics in CI.** Per-release deltas published as a status doc: file count, LOC per module, LOC per Pal, LOC per test, ratio of new tests to new code, modules added vs modules removed. Any metric trending the wrong way across three consecutive releases is flagged in the next retrospective.

**The verifier obeys the same rules it enforces.** Adding a new pre-commit hook, CI gate, or analysis script requires the same structural gate as adding a new module: what it replaces, what it extends. Without this rule the guardrails framework drifts the way `deploy/` drifted in v1.

None of these is sufficient on its own. Together they create the friction that prevents drift from accumulating below the threshold where anyone notices it.
