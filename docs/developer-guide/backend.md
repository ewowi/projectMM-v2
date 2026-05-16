# Backend

Implementation detail for the C++ runtime. Read [architecture/system.md](../architecture/system.md) first for the constraints and the model; this page covers the how.

---

## Data sharing

The architecture principle is in [system.md — Hot-path data sharing between modules](../architecture/system.md#hot-path-data-sharing-between-modules). This section covers the two primitives that implement it.

### `DataBuffer<T>` — the shared-slot primitive

`src/core/DataBuffer.h`. One pre-allocated slot of `T[]`, synchronised with a single atomic publish/acquire pair.

```cpp
T*       acquire_write();     // writer: pointer to the single slot; no alloc
void     publish();           // writer: one atomic store (memory_order_release)
const T* try_acquire_read();  // reader: two atomic loads; nullptr if no new frame
void     release_read();      // reader: one atomic store (memory_order_release)
uint32_t revision();          // monotonically increasing; detects geometry changes
```

**Allocation.** Call `allocate(count)` in `setup()` or `onAllocateMemory()`. The hot path never touches the heap.

**Synchronisation.** `publish()` pairs with `try_acquire_read()` via release/acquire ordering. This is the minimum to make a write visible on a second core without a lock. On Xtensa LX6/LX7 (ESP32) both are single-instruction 32-bit atomics.

**Torn-frame detection.** On a single core, producer and consumer share a slot without double-buffering. The consumer reads `revision()` before and after its work; if it changed, the producer wrote concurrently and the frame is discarded.

**Multiple readers.** Each reader calls `try_acquire_read()` and `release_read()` independently. `consumed_` tracks per-instance state so readers do not interfere with each other.

### `DataRegistry` — the name directory

`src/core/DataRegistry.h`. A flat array (up to 16 entries) mapping string ids to `DataBuffer` instances and their geometry.

```cpp
// Producer — called once from onAllocateMemory():
DataRegistry::instance().declare(id(), buf_, count, sizeof(T), w, h, d);

// Consumer — called from setup(), cached thereafter:
const DataBufferEntry* e = DataRegistry::instance().resolve("ripples-0");
buf_ = static_cast<DataBuffer<RGB>*>(e->buf_ptr);
```

`declare` and `undeclare` run on module-management paths (serialised by `ModuleManager`). `resolve` is read-only and safe to call from `loop20ms()` as a lazy fallback, though caching the pointer in `setup()` is preferred.

The `DataBufferEntry` carries `dim[3]` — a generic `{width, height, depth}` triplet set by the producer and read by consumers (e.g. `PreviewModule` uses it to size the WebSocket frame header).

### Usage pattern

```cpp
// Producer (e.g. RipplesEffect):
void onAllocateMemory() override {
    buf_ = new DataBuffer<RGB>();
    buf_->allocate(w * h * d);
    DataRegistry::instance().declare(id(), buf_, w*h*d, sizeof(RGB), w, h, d);
}
void loop20ms() override {
    RGB* dst = buf_->acquire_write();
    // ... fill dst ...
    buf_->publish();
}
void teardown() override {
    DataRegistry::instance().undeclare(buf_);
    delete buf_;
}

// Consumer (e.g. ArtnetOutModule):
void setup() override { resolve_buf_(); }
void loop20ms() override {
    if (!buf_) resolve_buf_();
    const RGB* src = buf_->try_acquire_read();
    if (!src) return;
    // ... map src into own buffer or wire format ...
    buf_->release_read();
}
void resolve_buf_() {
    const DataBufferEntry* e = DataRegistry::instance().resolve(source_id_);
    if (e) buf_ = static_cast<DataBuffer<RGB>*>(e->buf_ptr);
}
```

### Layering

Each **EffectLayer** owns a `PixelMap[]` array that maps virtual pixel indices (the coordinate space the effect writes into) to physical pixel indices in the EffectLayer's output buffer. The table encodes both the LayoutLayer's wiring map (`map(logical_idx) → physical_idx`) and any geometric modifiers (mirror, rotate, transpose) attached to the layer.

```cpp
struct PixelMap {
  uint32_t src_idx;   // index into the EffectLayer's virtual pixel buffer
  uint32_t dst_idx;   // index into the EffectLayer's physical output buffer
};
```

The **DriverLayer** reads each linked EffectLayer's buffer and applies that layer's `PixelMap[]` during a single copy pass into its own output buffer. Multiple EffectLayers are blended (blend mode per layer) in that same pass — no intermediate buffers.

```
EffectLayer.vbuf ──(PixelMap[])──→ EffectLayer.buf ──(DataBufferReader)──→ DriverLayer.buf
```

`PixelMap[]` is built cold-path in `onAllocateMemory` and rebuilt in `onUpdate` whenever the LayoutLayer or modifier set changes. Hot path: one table walk per EffectLayer, no branching, no function calls.

No change to `DataBuffer` or `DataRegistry` is required; the registry already handles multiple producers and each module owns exactly one slot. See [backlog — Light domain architecture](../development/backlog.md#light-domain-architecture) for the three-step implementation plan.

---

## Module lifecycle

See [architecture/system.md — MoonModule](../architecture/system.md#moonmodule-the-contract) for the contract. The sequence the runtime drives:

```
setup()
  → onBuildControls()     load controls, seed from persisted state
    → (recurse children)
  → onChildrenReady()
  → onAllocateMemory()    size and allocate buffers from final control values
loop*() ...               hot path — no alloc, no block
teardown()                free everything allocated in setup / onAllocateMemory
```

`onAllocateMemory` runs after controls are seeded so a module sizing its buffer from a control value (e.g. `RipplesEffect` allocating `w·h·d` RGB from its width/height/depth sliders) sees the correct value on the first allocation.

---

## Control field types and `addControl` overloads

`addControl(field, key, uiType, lo, hi)` resolves to the overload matching the **exact type of the backing field**. The overload set is:

| Field type   | `CtrlType` tag | Use for                                      |
|--------------|----------------|----------------------------------------------|
| `float`      | `Float`        | fractional values (speed, hue, brightness)   |
| `uint8_t`    | `Uint8`        | small counts, per-channel values (0–255)     |
| `uint16_t`   | `Uint16`       | panel dimensions: width, height, depth (up to 65535) |
| `uint32_t`   | `Uint32`       | total pixel counts (w × h × d, up to ~4 M)  |
| `bool`       | `Bool`         | toggles (enabled, serpentine)                |

**Convention for geometry fields:**

- `width_`, `height_`, `depth_` — `uint16_t`. All three axes uniform; supports up to 65535 per axis; two bytes each on ESP32.
- Total pixel count (`w * h * d`, buffers, loop indices) — `uint32_t`. Required on PC for large screens; promotes cleanly from `uint16_t` dimensions.

**Why the type must match exactly:** if the field type has no lvalue-ref overload (e.g. a hypothetical `uint16_t` field before the `Uint16` overload was added), the compiler promotes the field to an rvalue and falls through to a `FloatConst` display-only overload — silently making the control read-only. Always declare backing fields as one of the types in the table above.
