#pragma once
//
// DataRegistry — string-keyed registry of DataBuffer<T> instances + geometry.
//
// Producers (effect modules) call `declare(id, ...)` from onAllocateMemory()
// and `undeclare(id)` from teardown(). Consumers call `resolve(id)` from
// setup() and as a fallback in loop20ms so module-add order doesn't matter.
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

#include "DataBuffer.h"

namespace pmm {

// Geometry alongside a buffer — kept generic (count + element size) so
// DataRegistry itself does not import RGB or any lights-domain type.
struct DataBufferEntry {
  const char* id       = nullptr;  // points into the producer module's id_ (stable)
  void*       buf_ptr  = nullptr;  // DataBuffer<T>* — type-erased; cast by caller
  size_t      count    = 0;        // number of elements per slot
  size_t      elem_size = 0;       // sizeof(T)
  // Optional geometry metadata — set by producer, read by consumers.
  // Kept as generic uint16 triplet; interpretation is domain-specific (w/h/d for pixels).
  uint16_t    dim[3]   = {};       // e.g. {width, height, depth} for RGB buffers
};

class DataRegistry {
 public:
  static DataRegistry& instance() {
    static DataRegistry r;
    return r;
  }

  // Declare (or re-declare on geometry change) a buffer for `id`.
  // `buf` must be a heap-allocated DataBuffer<T>* owned by the caller (the
  // producer module's onAllocateMemory). DataRegistry stores the pointer but
  // does NOT own it — the producer is responsible for allocation and free.
  // Returns the entry so callers can check buf->valid().
  // dim0/dim1/dim2: optional geometry (e.g. width/height/depth for pixel buffers).
  DataBufferEntry* declare(const char* id, void* buf_ptr,
                           size_t count, size_t elem_size,
                           uint16_t dim0 = 0, uint16_t dim1 = 0, uint16_t dim2 = 0) {
    if (!id || !buf_ptr) return nullptr;
    for (uint8_t i = 0; i < count_; ++i) {
      auto& e = entries_[i];
      if (e.id && std::strcmp(e.id, id) == 0) {
        e.buf_ptr  = buf_ptr;
        e.count    = count;
        e.elem_size = elem_size;
        e.dim[0] = dim0; e.dim[1] = dim1; e.dim[2] = dim2;
        return &e;
      }
    }
    if (count_ < kMaxEntries) {
      entries_[count_] = {id, buf_ptr, count, elem_size, {dim0, dim1, dim2}};
      return &entries_[count_++];
    }
    return nullptr;  // table full — increase kMaxEntries
  }

  // Remove the entry for this buffer pointer. Called from producer teardown().
  void undeclare(void* buf_ptr) {
    for (uint8_t i = 0; i < count_; ++i) {
      if (entries_[i].buf_ptr == buf_ptr) {
        entries_[i] = entries_[--count_];
        entries_[count_] = {};
        return;
      }
    }
  }

  // Look up by id. Returns nullptr if not found or not yet declared.
  // Callers cast buf_ptr to DataBuffer<T>* themselves — avoids importing T here.
  const DataBufferEntry* resolve(const char* id) const {
    if (!id || !*id) return nullptr;
    for (uint8_t i = 0; i < count_; ++i) {
      if (entries_[i].id && std::strcmp(entries_[i].id, id) == 0)
        return &entries_[i];
    }
    return nullptr;
  }

 private:
  static constexpr uint8_t kMaxEntries = 16;
  DataBufferEntry entries_[kMaxEntries] = {};
  uint8_t         count_ = 0;
};

}  // namespace pmm
