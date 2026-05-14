#pragma once
//
// DataRegistry — string-keyed registry of DataRing<T> instances + geometry.
//
// Producers (effect modules) call `declare(id, count, depth)` from
// onAllocateMemory() and `undeclare(id)` from teardown(). Consumers call
// `resolve(id)` from setup() and as a fallback in loop20ms so module-add
// order doesn't matter.
//
// Ring depth convention (callers may override):
//   pal::psram_size() > 0  →  depth 2 (double-buffer, safe cross-core)
//   pal::psram_size() == 0 →  depth 1 (single slot, same-core only)
//
// Thread safety: declare/undeclare run on module-management paths
// (setup/teardown) which are already serialised by ModuleManager. resolve()
// may be called from loop20ms but only reads; the rare concurrent access
// is covered by the caller's existing serialisation. No internal locking.
//
// Hot-path note: resolve() is NOT called from the inner pixel loop —
// callers cache the pointer from setup()/onUpdate() and use it directly in
// loop20ms. This file imposes zero hot-path cost.
//

#include <cstring>

#include "DataRing.h"
#include "../pal/PalSystemInfo.h"

namespace pmm {

// Geometry alongside a ring — kept generic (count + element size) so
// DataRegistry itself does not import RGB or any lights-domain type.
struct DataRingEntry {
  const char* id        = nullptr;  // points into the producer module's id_ (stable)
  void*       ring_ptr  = nullptr;  // DataRing<T>* — type-erased; cast by caller
  size_t      count     = 0;        // number of elements per slot
  size_t      elem_size = 0;        // sizeof(T)
  uint8_t     depth     = 0;
  // Optional geometry metadata — set by producer, read by consumers.
  // Kept as generic uint16 triplet; interpretation is domain-specific (w/h/d for pixels).
  uint16_t    dim[3]    = {};       // e.g. {width, height, depth} for RGB rings
};

class DataRegistry {
 public:
  static DataRegistry& instance() {
    static DataRegistry r;
    return r;
  }

  // Default depth: 2 when PSRAM is present (cross-core safe), 1 otherwise.
  static uint8_t default_depth() {
    return pal::total_psram_kb() > 0 ? 2 : 1;
  }

  // Declare (or re-declare on geometry change) a ring for `id`.
  // `ring` must be a heap-allocated DataRing<T>* owned by the caller (the
  // producer module's onAllocateMemory). DataRegistry stores the pointer but
  // does NOT own it — the producer is responsible for allocation and free.
  // Returns the entry so callers can check ring->valid().
  // dim0/dim1/dim2: optional geometry (e.g. width/height/depth for pixel rings).
  DataRingEntry* declare(const char* id, void* ring_ptr,
                         size_t count, size_t elem_size, uint8_t depth,
                         uint16_t dim0 = 0, uint16_t dim1 = 0, uint16_t dim2 = 0) {
    if (!id || !ring_ptr) return nullptr;
    for (uint8_t i = 0; i < count_; ++i) {
      auto& e = entries_[i];
      if (e.id && std::strcmp(e.id, id) == 0) {
        e.ring_ptr  = ring_ptr;
        e.count     = count;
        e.elem_size = elem_size;
        e.depth     = depth;
        e.dim[0] = dim0; e.dim[1] = dim1; e.dim[2] = dim2;
        return &e;
      }
    }
    // Append — linear scan fine: typical scene has < 8 producers.
    if (count_ < kMaxEntries) {
      entries_[count_] = {id, ring_ptr, count, elem_size, depth, {dim0, dim1, dim2}};
      return &entries_[count_++];
    }
    return nullptr;  // table full — increase kMaxEntries
  }

  // Remove the entry for this ring pointer. Called from producer teardown().
  void undeclare(void* ring_ptr) {
    for (uint8_t i = 0; i < count_; ++i) {
      if (entries_[i].ring_ptr == ring_ptr) {
        // Compact: swap with last
        entries_[i] = entries_[--count_];
        entries_[count_] = {};
        return;
      }
    }
  }

  // Look up by id. Returns nullptr if not found or not yet declared.
  // Callers cast ring_ptr to DataRing<T>* themselves — avoids importing T here.
  const DataRingEntry* resolve(const char* id) const {
    if (!id || !*id) return nullptr;
    for (uint8_t i = 0; i < count_; ++i) {
      if (entries_[i].id && std::strcmp(entries_[i].id, id) == 0)
        return &entries_[i];
    }
    return nullptr;
  }

 private:
  static constexpr uint8_t kMaxEntries = 16;
  DataRingEntry entries_[kMaxEntries] = {};
  uint8_t       count_ = 0;
};

}  // namespace pmm
