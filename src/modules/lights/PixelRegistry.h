#pragma once
//
// PixelRegistry — cross-module lookup of `PixelSource` instances by module id.
//
// Producers (effect modules) call `publish(id, this)` from setup() and
// `unpublish(this)` from teardown(). Consumers call `find(id)` from setup()
// and on `source` control updates (and once as a fallback in loop20ms so
// module-add order doesn't matter).
//
// Why a static registry instead of dynamic_cast: arduino-esp32 builds with
// -fno-rtti, so `dynamic_cast<PixelSource*>(manager_->find(id))` does not
// link. Adding a virtual `asPixelSource()` to MoonModule would violate the
// Sprint 6 "nothing in core" constraint. A small file-local registry inside
// modules/lights/ keeps the abstraction in the lighting domain, where it
// belongs.
//
// Thread safety: publish / unpublish happen on setup/teardown (module-mgmt
// path, holds ModuleManager mutex). find() is called from setup, onUpdate,
// and the consumer's loop1s/loop20ms — also off the hot pixel-write path.
// No locking inside the registry itself; the caller's existing locks
// (ModuleManager::mutex) cover the rare concurrent access.
//

#include <cstring>
#include <vector>

#include "Pixelable.h"

namespace pmm {

class PixelRegistry {
 public:
  static PixelRegistry& instance() {
    static PixelRegistry r;
    return r;
  }

  void publish(const char* id, PixelSource* src) {
    if (!id || !src) return;
    // Replace any existing entry for this id (paranoia — usually unique).
    for (auto& e : entries_) {
      if (std::strcmp(e.id, id) == 0) { e.src = src; return; }
    }
    entries_.push_back({id, src});
  }

  void unpublish(PixelSource* src) {
    if (!src) return;
    for (auto it = entries_.begin(); it != entries_.end(); ) {
      if (it->src == src) it = entries_.erase(it);
      else                ++it;
    }
  }

  PixelSource* find(const char* id) const {
    if (!id || !*id) return nullptr;
    for (const auto& e : entries_) {
      if (std::strcmp(e.id, id) == 0) return e.src;
    }
    return nullptr;
  }

 private:
  struct Entry { const char* id; PixelSource* src; };  // `id` points into the module's owned id_ buffer; valid for the lifetime of the registration
  std::vector<Entry> entries_;
};

}  // namespace pmm
