# Development

projectMM v2 follows an Agile process adapted for AI-collaborative development. Releases are major milestones; sprints are scope-boxed work cycles within a release.

## The contract

[Process architecture](../architecture/process.md) is the contract under which every release executes — frugality (the main driver), the guardrails that enforce it, and the anti-drift rules that keep the framework itself from eroding. It is not a release artefact but a constraint on every release that follows. Any pull request that bypasses or relaxes it carries a paired ADR file (`docs/adr/NNNN-*.md`).

## Releases

| Release | Theme | Status | Tag |
|---|---|---|---|
| [Release 1](release-01.md) | Restart to parity with v1 | Planned | — |

Release 5 is the next recurring evaluation sprint per the [process architecture](../architecture/process.md) (every fifth release of v2). It is scheduled in Release 1 Sprint 1, not at the end of Release 4.
