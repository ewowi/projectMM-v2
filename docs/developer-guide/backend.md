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

`map_blend` is a hot-path free function in `core/`: it takes an array of source buffer pointers, a count, a destination buffer, and a blend function argument.

```cpp
map_blend(src[], n, dst, blend_fn);   // n effect buffers → one driver buffer
```

The blend function is domain-specific (`modules/lights/`); `map_blend` itself is generic. At minimum — one effect layer, one driver layer — this gives two buffers and true 2-core parallelism on any device where both fit in memory.

No change to `DataBuffer` or `DataRegistry` is required; the registry already handles multiple producers and each module already owns exactly one slot.

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
