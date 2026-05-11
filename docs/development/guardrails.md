# Guardrails framework

> **Source:** ported verbatim from v1's [Release 9 § Guardrails framework outline](https://ewowi.github.io/projectMM/development/release-09/#guardrails-framework-outline). This document is the spec that Release 1 Sprint 1 implements. Changes require a paired ADR.

---

Three enforcement tiers, all in place before the first module of v2 is written.

## Pre-commit {#pre-commit}

Git hooks that fire before the AI agent finishes the diff.

- `clang-format`, `ruff` — formatting and lint.
- No `pinMode(<integer>)`, `digitalWrite(<integer>)`, `gpio_set_*(<integer>)`, or other GPIO call with a literal pin — pins must come from a typed board configuration.
- No `new` / `malloc` / `psram_malloc` / `JsonDocument` inside the body of any `loop()`, `loop20ms()`, `loop1s()`, or `loop10s()` (heuristic regex on `void <name>::loop\b.*\{` … matching `}`).
- No `delay`, `vTaskDelay`, `sleep`, `usleep`, blocking `recv` with positive timeout inside the same hot-path bodies.

## CI gates {#ci-gates}

Block PR merge.

- `cppcheck`, add `clang-tidy` with `bugprone-*` and `modernize-*` from commit 1.
- **Module footprint baseline.** `classSize()` per module written to `baselines/footprint.json`. PR fails if any value grows without a paired entry in the PR description.
- **Test-count baseline.** Per-binary test count tracked; a new module bumps the count by exactly N expected tests; mismatch fails the PR.
- **Doc growth budget.** Docs lines per release capped (proposal: 500); CI fails on overshoot, override requires explicit budget bump in the release plan.

## Structural additions {#structural-additions}

Block additions that bypass the design.

- Adding a new file under `deploy/`, `tests/`, or `docs/` requires a `// WHY:` (C++) or top-of-file Python docstring explaining what existing file/surface was insufficient. CI checks the line exists; reviewer judges the content.
- Adding a new top-level directory requires an ADR file (`docs/adr/NNNN-*.md`) under version control.

---

The verifier-of-the-verifier question ("test the testing system?") gets a one-line answer: the testing system's growth is gated by the structural rule above (a new test surface requires a written justification), and its correctness is asserted only by the unit test that every module's `healthReport()` is non-empty. No meta-tests.
