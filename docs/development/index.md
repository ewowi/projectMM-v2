# Development

projectMM v2 follows an Agile process adapted for AI-collaborative development. Releases are major milestones; sprints are scope-boxed work cycles within a release.

## Constraints that travel with every release

Two documents are not release artefacts but constraints on every release that follows. They were produced in v1's [Release 9](https://ewowi.github.io/projectMM/development/release-09/) and ported here verbatim because they apply to v2 from commit 1.

- [Anti-drift rules](anti-drift.md) — the mechanisms that keep frugality from eroding once AI agents do most of the writing.
- [Guardrails framework](guardrails.md) — the pre-commit, CI, and structural gates that enforce those mechanisms.

Any pull request that bypasses or relaxes either document carries a paired ADR file (`docs/adr/NNNN-*.md`).

## Releases

| Release | Theme | Status | Tag |
|---|---|---|---|
| [Release 1](release-01.md) | Restart to parity with v1 | Planned | — |

Release 5 is the next recurring evaluation sprint per the anti-drift schedule (every fifth release of v2). It is scheduled in Release 1 Sprint 1, not at the end of Release 4.
