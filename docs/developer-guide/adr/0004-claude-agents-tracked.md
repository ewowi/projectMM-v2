# ADR 0004 — Track `.claude/agents/` as a committed top-level path

**Status:** Accepted, 2026-05-17.
**Supersedes:** none.
**Related:** [process architecture §2 — guardrails obey their own rules](../../architecture/process.md); [CLAUDE.md](https://github.com/ewowi/projectMM-v2/blob/main/CLAUDE.md) Rule #1; [structure check](https://github.com/ewowi/projectMM-v2/blob/main/scripts/checks/check_structure.py).

## Context

projectMM v2 is developed with Claude Code as a first-class part of the workflow — the `docs/` tree is explicitly framed as agent memory, MoonDeck embeds an agent loop, and the release plan is authored with agent assistance. Two project-local agents proved their value during the Sprint 16/17 work and the scripts restructure:

- **`guardrail-runner`** — runs the *full* pre-push verification suite (the pre-commit checks **plus** `mkdocs build --strict`, `test.py`, and a `pc` build). It exists because two CI jobs failed reactively (a zlib-nondeterministic `check_bundle`, a broken `mkdocs --strict` link) when local verification only ran the pre-commit subset.
- **`minimalism-reviewer`** — reviews an uncommitted diff cold against Rule #1 (what was removed, LOC-budget discipline, architecture/ADR fit, hot-path bans, PATCH convention, scope creep).

These were initially added local-only (gitignored), reasoned as "solo dev, no consumer, an ADR would be ceremony without payoff." That reasoning is now obsolete: the maintainer intends the repo to (a) onboard additional contributors and (b) serve as a **showcase of agentic development**. Under both goals the agents are *product*, not personal config — they must be versioned with the code they check, visible in the tree, and shared by every clone.

`scripts/check_structure.py` blocks any tracked top-level path not in its `ALLOWED` set, and its own rule requires "a v1 carry-over or a paired ADR" for each entry. `.claude/` is neither a carry-over nor previously allowed. This ADR is that paired justification — the anti-drift guardrail working exactly as designed: a new tracked top-level directory is a deliberate, recorded decision, not a silent one.

Alternatives considered:

- **Keep `.claude/` local-only (gitignored).** Zero structural cost, but the agents do not travel with the repo, drift out of sync with the `scripts/` paths they invoke, and contribute nothing to the showcase goal. Rejected once a real consumer (collaborators + showcase) exists.
- **Put agents somewhere already allowed (e.g. `scripts/agents/` or `docs/`).** Claude Code only discovers agents in `.claude/agents/` (project) or `~/.claude/agents/` (user). Relocating them breaks discovery; a non-standard location would need its own shim and confuses the showcase. Rejected — fight the tool's convention, lose the "this is how agentic dev works" clarity.

## Decision

`.claude/agents/` is a tracked, committed directory. `.claude` is added to `scripts/check_structure.py`'s `ALLOWED` set with this ADR as its justification.

`.gitignore` is narrowed from `.claude/` (everything) to host-specific state only: `.claude/settings.local.json` and `.claude/*.local.json`. The `.local.` suffix is the established "personal, not shared" convention (settings.local.json was already untracked); shareable agent definitions are committed.

`.claude/` is reserved for Claude Code workspace configuration that is *intended to be shared* — agent definitions today, and (if added later, each with its own justification) slash commands, shared output styles, or hooks. It is **not** a general-purpose top-level dir for v2-authored runtime/tooling code, which lives in `src/` and `scripts/` respectively.

## Consequences

**Cost.**
- A new tracked top-level path (`.claude/`), and the `ALLOWED` allowlist grows by one entry.
- Agent prompt files are not LOC-budgeted (`check_loc.py` inspects `.py`/`.h`/`.cpp` under `src/`, `scripts/`, `test/`). Like `docs/`, they are prose; discipline is by review, not a mechanical budget. An agent file that bloats is a review finding.
- Committed agents reference `scripts/checks/*` and `scripts/build/*` paths; if those move again, the agents must be updated in the same change (as any path-coupled file is).

**Benefit.**
- Every clone gets the same verification + review agents — collaborators inherit the project's discipline instead of re-deriving it.
- The agents are versioned with the code they check; a path change and its agent update land together.
- The repo demonstrates agentic development concretely: the agents that enforce the project's rules are *in* the project, readable.
- `guardrail-runner` closes the specific gap that caused two reactive CI failures — full-suite verification is now a one-call, shared capability.

**What is *not* allowed under this ADR.**
- Committing host-specific or secret state under `.claude/` (device IPs, tokens, `settings.local.json`). Those stay gitignored.
- Using `.claude/` for v2-authored runtime code, scripts, or docs — it is Claude Code workspace config only. Runtime code → `src/`; tooling → `scripts/`; documentation → `docs/`.
- Adding further `.claude/` subtypes (commands, hooks, styles) silently. Each new *kind* of shared workspace config is a structural addition; note it in this ADR (or a follow-up) with its justification, same bar as a new module.

## Reaffirmed — no tester, deployer, scripts-owner, or product-owner agent

The original decision rejected a tester and a deployer agent; that rationale
lived only in the project record, not this ADR. Made explicit and durable
here, and broadened, because the proposal recurred even with a now-substantial
test surface (84 host cases, scenario REST runner, generated `tests.md`):

- **No tester agent.** Decompose what it would do: *run everything / report
  pass-fail* is exactly `guardrail-runner` (the v1 "4th test surface"
  pattern). *Write or migrate tests* is interactive write→run→adjust work —
  the deliberately-absent developer role (the Sonnet main thread does this;
  see `project_no_developer_agent`). *Decide what is under-tested / test
  strategy* is a one-shot design question already in `architect`'s scope.
  No residue is left for a tester agent. A larger suite *strengthens* this
  rejection (don't fork a second authority over it), not reverses it.
- **No single-agent ownership of `moondeck.py` + scripts + CI.** That is the
  v1 `deploy/`-concentration anti-pattern (one owner accreting an unbounded
  tooling surface). That surface is governed by the moondeck.py census + the
  structural-additions gate (process.md §2) + `check_structure.py`, not an
  owning persona.
- **No product-owner / end-user / PM agent, and no agent as conflict
  tiebreaker.** An agent-as-PO is a second source of truth that drifts from
  the documents — the named v1 drift. Product intent lives in
  [`architecture/product.md`](../../architecture/product.md); the authority
  chain (documents are the contract; agents produce findings not rulings;
  the maintainer ratifies via ADR) is recorded in
  [ADR 0008](0008-product-requirements-constraint.md). The team is the
  workflow, not headcount.
