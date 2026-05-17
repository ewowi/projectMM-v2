# ADR 0008 — Product requirements as an architectural constraint

**Status:** Accepted, 2026-05-17.
**Supersedes:** none.
**Refines:** the constitution — adds a third architecture document alongside
[system.md](../../architecture/system.md) and [process.md](../../architecture/process.md).
**Related:** [ADR 0004](0004-claude-agents-tracked.md) (agent roster — the
no-tester/deployer/PO-agent line); [CLAUDE.md](https://github.com/ewowi/projectMM-v2/blob/main/CLAUDE.md)
Rule #1; the no-developer-agent decision recorded in agent memory.

## Context

The project's end-user goals — capacity floors (≥4096 LEDs on a no-PSRAM
ESP32), throughput ceilings (P4 as the speed reference), a desktop-first
UX following recognised prior art (TouchDesigner / MADRIX), and visible
test transparency — were not recorded as a durable contract. They were
smeared across release-01 sprint notes as *shipped facts*, not stated as
*binding forward requirements*.

The maintainer proposed an "end-user agent" (and a tester agent, and making
the reviewer the product owner). Those were rejected: an agent that owns UI
code is the deleted developer-agent pattern; a tester agent duplicates
`guardrail-runner` or the main thread; an agent-as-PO creates a second
source of truth that drifts from the documents — the exact v1 anti-pattern
the constitution exists to prevent ("the architecture quietly becoming one
document among many", process.md §3). The product *intent* belongs in a
*document*, not a *persona*.

A capacity floor on a no-PSRAM target is the same kind of constraint as a
LOC budget or a per-module footprint baseline: a Rule #1 resource-minimalism
input that shapes every design. It therefore belongs in `architecture/`
(ADR-gated, changes never silent), not `development/` (churns per release,
explicitly "not durable contract").

## Decision

1. **`docs/architecture/product.md` is the third constitution document.**
   It states what the product must deliver. Every PR must fit it, the same
   as system.md and process.md. Changing a **binding** target is an
   architecture change and carries a paired ADR.

2. **Two tiers, explicitly marked.** *Binding* = a hard guarantee; a
   regression or a design that can't meet it is a constitutional conflict
   (rejected or ADR-amended). *Aspirational* = a stretch pursued only when
   minimalism allows; missing it is informational, never a blocker, never
   an ADR trigger, and must not silently become a de-facto floor. The
   ≥12288-LEDs-no-PSRAM target is the one aspirational entry; the rest are
   binding.

3. **Enforced by existing agents, no new agent.** `minimalism-reviewer`
   gains one checklist line: a binding perf/capacity regression is the same
   bar as an unjustified LOC-budget bump (ADR required); an aspirational
   miss is informational only. `architect` gains one line: a design that
   cannot meet a binding floor is a constitutional conflict needing an ADR,
   not a tradeoff (an aspirational shortfall *is* a tradeoff). No frontmatter
   "ownership" claims are added — the document is the authority; agents read
   and enforce it.

4. **Authority chain (made explicit, once, here).**
   - The documents are the contract: system.md + process.md + product.md +
     accepted ADRs. A conflict is resolved by what the documents say.
   - Doc-vs-doc precedence: architecture/ > developer-guide/ > development/
     (already stated in CLAUDE.md).
   - Agents are advisors mapped to a lens (`architect` = structural/design
     fit + doc/ADR conflicts; `minimalism-reviewer` = Rule #1/budget/scope
     on a concrete diff). They produce **findings, not rulings**. When they
     disagree, that is surfaced to the maintainer, not auto-resolved.
   - The maintainer ratifies. Any change to a constitution document, or an
     unresolved conflict, is decided by the maintainer via an ADR. **No
     agent is the product owner or the tiebreaker.** The closest thing to a
     PO is `product.md` itself — intent that was about to be vested in an
     agent is instead vested in an ADR-gated document.

## Consequences

- **What this ADR introduces:** one new doc (`architecture/product.md`),
  this ADR, one reaffirmation note appended to ADR 0004, and exactly **one
  product-enforcement line** in each of `minimalism-reviewer.md`
  (binding-regression = LOC-bump bar) and `architect.md` (binding-floor
  miss = constitutional conflict). Nav wiring only. Zero `src/`, zero new
  mechanism, no new agent. Net: *consolidates* perf intent previously
  scattered across release-01 sprint notes into one forward contract,
  *replaces* the rejected end-user/tester/PO agents with a document, and
  reuses the existing `tests.md`/HIL artefacts for the transparency target.
- **Scope honesty:** the agent files in the working tree also carry
  unrelated, prior-session guardrail-framework additions (the architect's
  "simplicity gate" and the reviewer's simpler-or-complex / established-
  pattern / ADR-proliferation checklist items). Those are **not** part of
  this ADR and are not justified by it — they are a separate change that
  must land under its own justification (and committed separately from this
  product-requirements change; one change = one reviewable unit).
- Every future binding-target change is ADR-gated — deliberately heavy,
  correct for a hard guarantee. Aspirational targets stay re-tunable with
  no ADR.
- The "team of personas with a boss" mental model is explicitly rejected
  for this project: structure lives in documents, not roles ("the team is
  the workflow, not headcount", ADR 0004).

## What is *not* allowed under this ADR

- An end-user / product-owner / PM agent. The intent is the document.
- A tester or deployer agent (see the reaffirmed note in ADR 0004).
- An agent acting as conflict tiebreaker over the documents.
- Treating an aspirational target as a blocking finding, or a binding
  target change without an ADR.
