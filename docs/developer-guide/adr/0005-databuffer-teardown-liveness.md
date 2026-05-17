# ADR 0005 — `DataBuffer<T>` teardown liveness

**Status:** Accepted, 2026-05-17.
**Supersedes:** none.
**Refines:** [ADR 0003 — `DataBuffer<T>` and `DataRegistry` in core](0003-dataring-dataregistry-in-core.md).
**Related:** [architecture/system.md](../../architecture/system.md) (hot-path data sharing; grouping/layer pattern); [CLAUDE.md](https://github.com/ewowi/projectMM-v2/blob/main/CLAUDE.md) Rule #1.

## Context

ADR 0003 established the zero-copy pixel path: a producer module owns one
`DataBuffer<T>`, `DataRegistry` is a non-owning name→pointer store ("structurally
identical to `ModuleManager` — owns resolution by name"), and each consumer owns
its own `DataBufferReader<T>`. system.md keeps that ownership model: a group
module owns the `DataBuffer` on behalf of its children; a lone effect owning its
own buffer is an explicit, valid fallback.

ADR 0003 did **not** specify buffer lifetime at producer teardown. This left a
defect: when a producer module is removed (`ModuleManager::remove` →
`runTeardown()` → producer `teardown()` does `undeclare(buf_); delete buf_`), any
consumer that had cached the resolved `DataBuffer*` and attached its reader still
holds that pointer. Consumers only re-resolve `if (!buf_)`, so the stale non-null
pointer is never refreshed. On the next tick the consumer dereferences freed
PSRAM — observed on device as `Core 1 panic'ed (IllegalInstruction)` when
deleting `artnet-0` / `preview` (consumers of a `ripples` producer).

This is a deferred dangling-pointer use-after-free, **not** a data race:
`Scheduler` runs the whole per-core module batch under the same
`recursive_mutex` `ModuleManager::remove` takes, so removal and ticks are
mutually excluded. The window is "after producer teardown, before the consumer
re-resolves". `RipplesEffect` already worked around the *geometry-change* variant
of this same bug (it keeps `buf_` alive across resize); the module-*delete* path
had no such guard.

Alternatives considered and rejected:

- **`DataRegistry` owns the buffers (buffers outlive producers).** Rejected:
  contradicts system.md (producer/group owns the buffer; lone-child
  self-ownership is valid) and ADR 0003's decision text and "registry owns
  *resolution*, not the pointee" framing — that is a *supersede*, not a refine.
  It also pushes a typed allocator into a deliberately type-erased core
  primitive (`void*` + `dim[3]`, never sees `T`) — the v1 cross-domain-drift
  pattern ADR 0003 exists to prevent. And it splits buffer lifetime away from
  the subtree that drives top-down `onAllocateMemory`, breaking the group/layer
  end-state.
- **`ModuleManager` tracks reader→producer references and blocks/【defers】
  removal while a consumer is live.** Rejected: new cross-cutting state and
  consumer-graph knowledge in core for a problem solvable at the existing
  `DataBuffer` atomic; also makes module removal fail/block on live consumers,
  contradicting system.md's "drop is graceful, nothing silently lost".
- **Consumers re-resolve via `DataRegistry::resolve` every tick.** Rejected:
  contradicts ADR 0003 / DataRegistry.h hot-path note ("resolve() is NOT called
  from the inner pixel loop"); an O(n) `strcmp` scan per consumer per
  `loop20ms`, and still leaves a window without the sentinel below.

## Decision

The ownership model is **unchanged**: the producer/owner (lone effect today,
group parent in the system.md end-state) owns and frees its `DataBuffer<T>`;
`DataRegistry` stays non-owning; each consumer owns its reader. ADR 0003 stands.

Add a **liveness invariant** to the data-sharing contract:

1. **Producer teardown ordering.** A producer MUST, inside `teardown()` and
   before freeing the buffer, call `DataRegistry::undeclare(buf_)` then
   `buf_->invalidate()`, then `delete buf_`. (`ModuleManager::remove` runs
   `teardown()` under the serialising mutex, children-first.)
2. **Liveness rides the existing atomic.** `DataBuffer::invalidate()` stores a
   `kDead` sentinel into the same `published_` atomic that
   `DataBufferReader::try_acquire_read()` already loads on the hot path. A
   reader observing `kNone` or `kDead` returns `nullptr` (graceful skipped
   frame) instead of touching `slot_`. No new atomic, no new hot-path load —
   the sentinel shares the revision word.
3. **Consumers detach on death.** Consumers poll `DataBufferReader::dead()` on
   their existing non-hot cadence (`loop20ms` entry, off the inner pixel loop),
   drop the cached `buf_`, `detach()` the reader, and re-resolve — finding
   nothing until a producer reclaims the id.

The sentinel covers the "already resolved, not yet re-checked" tick; the
re-resolve covers steady state. Together, under the existing scheduler
serialisation, the lifetime gap is closed for both the lone-child fallback and
the group/layer end-state under one rule — no second mechanism for grouping.

## Consequences

- `src/core/DataBuffer.h`: adds `kDead`, `invalidate()`, a `dead()` accessor on
  the reader, and one extra equality test in `try_acquire_read()`. No new file;
  `check_loc.py` core budget unaffected (single existing file, small delta).
- `src/core/DataRegistry.h`: **unchanged** (ownership untouched).
- Producers (`RipplesEffect` + the five `effects/*` producers): one
  `buf_->invalidate()` call added between `undeclare` and `delete` in
  `teardown()`.
- Consumers (`ArtnetOutModule`, `PreviewModule`): one guarded
  `if (buf_ && reader_.dead()) { buf_ = nullptr; reader_.detach(); }` at
  `loop20ms` entry.
- `published_` now carries two meanings (frame revision + a dead sentinel). This
  is a deliberate semantic compression: the hot path already loads that word, so
  liveness costs zero extra loads. A separate `std::atomic<bool>` was the
  clearer-but-costlier alternative; minimalism wins.
- This is a true fix that makes the SPSC lifetime contract explicit, not a
  `// PATCH:` over a symptom. ADR 0005 *is* the contract.

## What is *not* changed

- Buffer ownership (still producer/group; ADR 0003 and system.md stand).
- `DataRegistry` remains a non-owning name→pointer store.
- No `ModuleManager` change, no `Scheduler` change, no system.md edit — the
  fix adds a safety invariant without altering the architecture model, so per
  CLAUDE.md it is a new ADR refining ADR 0003, not a system.md change and not a
  superseding ADR.
