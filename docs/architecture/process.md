# Process Architecture

How projectMM v2 is built and changed. This page is a constraint on every release. A pull request that violates it either is rejected or carries a paired ADR file (`docs/developer-guide/adr/NNNN-*.md`) recording the change explicitly.

The [system architecture](system.md) constrains the artifact. This page constrains how the artifact changes.

---

## 1. Minimalism — the main driver

**Minimal in code, in CPU cycles, and in resources.** Every change answers two questions before it lands:

1. Does this addition pay for itself? A new file, test surface, doc page, or deploy script — what does it replace or extend?
2. What did you remove? Every release removes at least one thing. A release that adds without removing must justify why.

If the answer to (1) is "nothing", the PR is rejected.

The three dimensions are tested differently:

- **Code minimalism** — whether a reader can hold the core in their head. Measured in LOC budgets per surface and in reviewer cognitive load.
- **CPU minimalism** — `loop()` time under realistic load on every active core.
- **Resource minimalism** — static and runtime footprint per module across RAM, flash, and any other constrained resource. Concretely: prefer `uint16_t` / `uint8_t` over `float` or `int` where the value fits; prefer `int` over `float` where the value is integral; use `float` only where fractional precision is actually needed. Floats cost 4 bytes each in the struct, require the FPU on the hot path, and accumulate rounding error when used as counters or indices.

Minimalism applies equally to source, tests, deploy scripts, and documentation. The principle is not new: essential complexity only (Brooks), Gall's Law, and the Saint-Exupéry rule that perfection is reached not when nothing more can be added but when nothing more can be taken away.

---

## 2. Guardrails — minimalism enforced mechanically

Minimalism is a property that erodes silently. Guardrails make the erosion mechanical to detect. They land in Sprint 1 of Release 1, before any module exists; no commit in v2 is ever written without them running against it.

Three enforcement tiers, in order of when they fire:

- **Pre-commit** — fires before the diff is finished. Catches violations of the hot-path rules (no allocations, no blocking calls, no logging in any `loop*()` body), architectural fitness functions (pin numbers trace to typed board config — `pinMode(5, ...)` is rejected, `pinMode(BoardPins::WS2812_DATA, ...)` is the correct form), and the platform-isolation rule (`#ifdef ARDUINO`, `#include <Arduino.h>`, ESP-IDF headers, and similar platform-identity gates appear only in `src/pal/` files — see [system architecture](system.md#pal-the-only-place-platform-conditionals-appear)).
- **CI gates** — block PR merge. Enforce LOC budgets per surface, footprint baselines per module, and a doc-growth budget per release. Overshoot fails; a bump requires an explicit signed-off line in the release plan. Test infrastructure — frameworks, helpers, metrics, baseline files — obeys the same minimalism rule as source code: a new test file, helper, or CI metric must answer "what drift does this catch that the existing guardrails miss?"
- **Structural additions** — block additions that bypass the design. A new top-level directory requires an ADR. A new file under `scripts/`, `tests/`, or `docs/` requires an inline justification of what existing surface was insufficient.
- **Process visibility** — every developer-facing process (build, test, checks, doc-serve, future flash/upload/log-tail) is a card in `scripts/moondeck.py`. The UI is the process layer's census: adding a script is visible work; removing one is visible work. v1 reached 20+ `deploy/` scripts partly because the surface was never rendered in one place; v2 renders it from day one.

The specific tools that implement each tier (linters, formatters, static analysers, hook scripts) are an implementation detail of Sprint 1, not a constraint. They earn their place by paying for themselves under the same minimalism rule they enforce.

---

## 3. Anti-drift — why these rules survive

The guardrails above describe *what* is enforced. Anti-drift describes *why the framework itself does not erode* — the lesson from v1, where the verifier always trailed the code it was meant to constrain.

- **Guardrails ahead, never behind.** The framework ships before the code it constrains. This inverts the v1 timeline.
- **Mandatory subtraction.** Every release removes at least one thing. "What did you remove?" is a sprint-close question alongside "what did you add?". A system that never removes anything will always grow until someone considers a restart.
- **Minimalism is a review gate.** Every PR answers the two questions in §1 before any other.
- **Fix causes, not symptoms.** When a change does not behave correctly, find why and fix it at the cause. Do not add code that masks a symptom — every such patch creates a new failure mode that needs its own patch, and the system grows by accretion. Patterns to refuse: the retry added because something is flaky, the timer added because something races, the try/catch that swallows an error "just in case", the re-init that papers over inconsistent state. If the cause cannot be identified, the work is blocked — not a chance to add a band-aid.
- **Architecture is a constraint, not a description.** [System architecture](system.md) is the reference for every review. Changes to it happen only via ADR — never silently. v1's drift came from the architecture quietly becoming one document among many.
- **Recurring evaluation releases.** Every fifth release is a no-feature evaluation that walks the code, checks for drift, and proposes removals. The schedule is set at the start of v2 Sprint 1, not at the end of Release 4.
- **Drift metrics in CI.** Per-release deltas published as a status doc: file count, LOC per surface, ratio of new tests to new code, modules added vs removed. Any metric trending the wrong way across three consecutive releases is flagged in the next retrospective.
- **The verifier obeys the same rules it enforces.** Adding a new pre-commit hook, CI gate, or analysis script requires the same justification as adding a new module: what it replaces, what it extends. Without this rule the guardrails framework drifts the way `deploy/` drifted in v1.

None of these is sufficient on its own. Together they create the friction that prevents drift from accumulating below the threshold where anyone notices it.

---

## 4. Port-and-minimize — where substantive modules come from

v2 is not a greenfield rewrite of v1. The first attempt at Sprint 2 and Sprint 3 of Release 1 tried that path and produced buggy distillations of code v1 had already debugged into stability (TCP fragmentation, WS handshake corner cases, threading races). The minimalism principle was being applied to a blank page; it should have been applied to v1's working code.

The rule: any module above trivial size is *ported* from v1, not rewritten. Ported means brought in unchanged, then stripped until it pays for itself.

The discipline that prevents this from degrading into copying:

- **Copy first, then strip.** Bring the v1 file into v2 unchanged in the first commit. Read it line by line. Strip in a follow-up commit. Do not rearrange before you understand — rearranging-before-understanding is how v1's drift happened.
- **Strip patches, not features.** The port-and-minimize target is v1 code that exists because something was patched over instead of fixed at the cause: retries around flaky behaviour, swallow-everything `try/catch` blocks, timers that paper over races, re-init paths that hide inconsistent state (the §3 list applied retroactively to v1). Each such patch is replaced by an architecture decision recorded in this document or a paired ADR — the cause goes away, the patch goes away with it. **Future-needed features are not subtraction targets.** If a later sprint in this release (or the next one) will use the code, leave it: stripping a working feature now means re-porting it later, which is the waste this doctrine exists to prevent. The goal of port-and-minimize is to fix v1's drift, not to delete v1's capability.
- **An empty port-and-minimize step is a valid outcome.** Sometimes the v1 file is already well-built and the honest answer is "nothing to strip." The PR description records the deliberation regardless: which structures were examined, why each was kept, whether any "patches over symptoms" were found. This is different from "copied without reading," which the PR description proves by naming the decisions examined.
- **Guardrails are the verifier.** The same LOC budgets, hot-path bans, GPIO ban, and structural-additions allowlist that govern new code govern ports. A port arriving over its surface's LOC budget fails CI; bumping requires the same signed-off release-plan line as any other bump.
- **The architecture is not negotiable.** [System architecture](system.md) defines what belongs in core vs. modules. v1 mixed networking and system info into `Pal.h`; v2 does not. Anything that would cross a v2 core boundary as it is ported needs an ADR — never silently. If the v1 file mixes concerns the v2 architecture separates, split it during the port.
- **What is *not* ported.** Trivial code — a 30-line helper, a hello module, a tiny test — is faster to write fresh than to port. The threshold is judgement: if v1's version is ≤ ~50 LOC *and* writing the v2 version fresh takes an afternoon, write it. Otherwise, port.
- **The core skeleton is exempt.** `Module`, `ModuleManager`, `Scheduler`, and `Pal` were re-decided in v2 (the boundary between core and modules is one of the things v2 fixes). They are not ports — they are the frame everything else is ported into.
