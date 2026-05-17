# What we test

Every unit test grouped by the module or core surface it verifies. Generated from the `TEST_CASE` / `SUBCASE` names in `test/test_pc/*.cpp` by `scripts/build/gen_test_list.py` — a static parse, no run. Re-run that script after adding or renaming a test.

## ArtnetOutModule (Art-Net OpDmx)

`test_artnet_packing.cpp` — 4 test case(s)

- ArtNet OpDmx header bytes match the spec for universe 0, 510 DMX
- ArtNet OpDmx header — universe increments fit in the low byte
- ArtNet OpDmx header — partial DMX byte count (e.g. last universe)
- ArtNet OpDmx: 64x64 frame splits into 25 universes (24 full + 1 partial)

## MoonModule control system

`test_controls.cpp` — 11 test case(s)

- setControl float writes through and fires onUpdate
- setControl uint8/uint32/bool write through (cast, no clamp)
- setControl unknown key is a no-op returning false
- setControl via JsonVariant takes the REST path
- EditStr control is writable via JsonVariant, not the float path
- getSchema emits id + a control entry per addControl with min/max/default
- select control: getSchema carries the options array + index value
- getControlValues is a flat {key:value} reflecting live fields
- clearControls + rebuild preserves the live value (pending props)
- schemaDirty: clearControls marks dirty, clearSchemaDirty resets it
- defVal is captured from the field initializer, not a later setControl

## DataBuffer / DataBufferReader (SPSC)

`test_data_buffer.cpp` — 8 test case(s)

- DataBuffer: allocate(0) leaves the buffer invalid
- DataBuffer: reader returns nullptr before first publish
- DataBuffer: publish then read returns the filled slot
- DataBuffer: acquire_write returns the same slot every time
- DataBuffer: revision increments on each publish
- DataBuffer: reader returns nullptr after release_read with no new publish
- DataBuffer: two readers are independent — one release does not affect the other
- DataBuffer: SPSC cross-core consumer never tears under paced producer

## HttpServerModule

`test_http.cpp` — 3 test case(s)

- HttpServerModule: GET / serves the gzipped frontend bundle
- HttpServerModule: GET /api/modules lists registered modules
- HttpServerModule: unknown route returns 404

## REST integration (control + module API)

`test_integration.cpp` — 1 test case(s)

- REST integration: control mutation + module API contract end-to-end
    - GET /api/types returns the registered type names
    - POST /api/control on a valid id+key → 200 ok:true
    - POST /api/control unknown module id → 404
    - POST /api/control missing id/key → 400
    - POST /api/control malformed JSON → 400
    - POST /api/modules duplicate id → 409 (the dup-id enforcement point)
    - POST /api/modules unknown type → 201 (v2 add() is permissive)
    - control change is observable via GET /api/modules afterwards

## MemTracker + classSize drift

`test_memtracker.cpp` — 4 test case(s)

- memtracker: snapshot() is callable and internally consistent
- memtracker: frag_pct stays within 0..100 and is 0 on a 0 heap
- techdebt: classSize() == sizeof(T) for every registered module type
- techdebt: an unregistered type is a base MoonModule sized accordingly

## ModuleManager + SystemStatusModule

`test_module.cpp` — 11 test case(s)

- ModuleManager: add base module
- ModuleManager: factory builds concrete type
- ModuleManager: manager pointer set on add
- ModuleManager: remove existing
- ModuleManager: remove non-existent
- ModuleManager: find
- ModuleManager: unknown type yields a base module with a truthful type()
- ModuleManager: remove() erases the whole subtree, not just the root
- SystemStatusModule: setup/loop/teardown lifecycle does not crash
- SystemStatusModule: exposes named controls and classSize == sizeof
- SystemStatusModule: getSchema after loop1s sampling is well-formed

## Pal — heap / PSRAM

`test_pal_heap.cpp` — 4 test case(s)

- PalHeap: psram_alloc(0) returns null without allocating
- PalHeap: psram_free(nullptr) is a no-op
- PalHeap: psram_alloc returns a writable, byte-addressable buffer
- PalHeap: paired alloc/free does not leak across many cycles

## PreviewModule (binary wire format)

`test_preview_wire.cpp` — 2 test case(s)

- PreviewModule wire format: 4x4 frame header + body bytes
- PreviewModule wire format: width and height LE encoding for 128x128

## Parent-input model (reparent / reorder)

`test_reparent.cpp` — 10 test case(s)

- reparent: parent flag set on the name-matched input
- reparent: rejected when no input is named for the parent type
- reparent: wildcard 'source' input matches any parent type
- reparent: exact name==type wins over generic 'source'
- reparent: promote to root clears the flag but KEEPS the value
- reparent: loop order — parent precedes child after reparent
- reparent: returns false for unknown id
- reorder children: nested reorder changes child order and loop order
- reparent: 'layout' input matches a parent by category, not just type
- reparent: exact name==type wins over a category match

## RipplesEffect / PixelEffectBase

`test_ripples_lut.cpp` — 5 test case(s)

- RipplesEffect: lifecycle adds and exposes a valid pixel buffer
- RipplesEffect: geometry change via GridLayoutModule reallocates and bumps revision
- RipplesEffect: loop20ms writes non-trivial pixel output
- RipplesEffect: hue_base change recolours the output
- RipplesEffect: DataRegistry declares buffer for consumers to find

## Scenario replay (whole-pipeline)

`test_scenarios.cpp` — 1 test case(s)

- Scenarios: every JSON in test/test_pc/scenarios/ replays clean

## Scheduler (core affinity)

`test_scheduler_affinity.cpp` — 2 test case(s)

- Scheduler affinity: two-core scenario ticks both modules
- Scheduler affinity: single-core scenario ignores out-of-range modules

## StateStoreModule (persistence)

`test_state_store.cpp` — 1 test case(s)

- StateStoreModule round-trip — control survives a simulated reboot

---

**67 test cases across 14 files.** PC host build (`uv run scripts/build/test.py`); the same scenario fixtures also replay over REST against a live device via `scripts/device/scenario.py`.
