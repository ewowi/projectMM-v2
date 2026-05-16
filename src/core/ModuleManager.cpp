#include "ModuleManager.h"

#include <cstdio>

namespace pmm {

MoonModule* ModuleManager::add(const char* type, const char* id) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  std::unique_ptr<MoonModule> m;
  auto it = factories_.find(type);
  if (it != factories_.end()) {
    m = it->second();
    m->type_ = it->first.c_str();  // stable: points into the factory map key
  } else {
    // Unknown type: store the requested type name so type() is truthful.
    // The key lives in unknown_type_ on the manager so the pointer is stable.
    unknown_types_.emplace_back(type);
    m = std::make_unique<MoonModule>();
    m->type_ = unknown_types_.back().c_str();
  }
  m->id_ = id;
  m->manager_ = this;
  m->runSetup();   // dispatch wrapper: setup() + onBuildControls() + register enabled_
                   // + children + onChildrenReady + onAllocateMemory
  std::printf("[mm] add: type=%s id=%s\n", type, id);
  MoonModule* raw = m.get();
  modules_.push_back(std::move(m));
  return raw;
}

bool ModuleManager::remove(const char* id) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  for (auto it = modules_.begin(); it != modules_.end(); ++it) {
    if ((*it)->id_ == id) {
      std::printf("[mm] remove: id=%s\n", id);
      (*it)->runTeardown();  // dispatch wrapper: recurses into children first
      modules_.erase(it);
      return true;
    }
  }
  std::printf("[mm] remove: id=%s not found\n", id);
  return false;
}

// -- Internal helpers --------------------------------------------------------

// Depth-first walk into out; src entries are moved (nulled) as they are taken.
static void dfs_(MoonModule* node,
                 std::vector<std::unique_ptr<MoonModule>>& src,
                 std::vector<std::unique_ptr<MoonModule>>& out) {
  for (auto& m : src) {
    if (m && m.get() == node) { out.push_back(std::move(m)); break; }
  }
  for (uint8_t i = 0; i < node->childCount(); ++i)
    dfs_(node->child(i), src, out);
}

// Re-sort modules_ to depth-first traversal order (root → children → …).
void ModuleManager::sort_depth_first_() {
  std::vector<std::unique_ptr<MoonModule>> out;
  out.reserve(modules_.size());
  std::vector<MoonModule*> roots;
  for (const auto& m : modules_)
    if (m && !m->parent_) roots.push_back(m.get());
  for (MoonModule* r : roots) dfs_(r, modules_, out);
  for (auto& m : modules_) if (m) out.push_back(std::move(m));  // safety net
  modules_ = std::move(out);
}

// Index of the input on `child` that can be the parent link. An input
// matches the parent if its key == parent's type (e.g. effect's "layout"
// input ↔ "layout"-type parent), OR its key is the conventional wildcard
// "source" (a type-agnostic "reads from whatever produces data" input —
// preview/artnet use it). A precise name==type match wins over a generic
// "source" if the child has both. -1 if no input matches — the signal to
// reject the reparent (a module nests only under a parent it has an input
// for). See architecture/system.md "Inputs, and the parent input".
int8_t ModuleManager::parent_input_idx_(const MoonModule* child,
                                         const char* parent_type) {
  if (!parent_type) return -1;
  int8_t source_idx = -1;
  for (uint8_t i = 0; i < child->controlCount_; ++i) {
    const ControlDescriptor& d = child->controls_[i];
    if (d.type != CtrlType::String && d.type != CtrlType::EditStr) continue;
    if (std::strcmp(d.key, parent_type) == 0) return (int8_t)i;  // exact wins
    if (source_idx < 0 && std::strcmp(d.key, "source") == 0) source_idx = (int8_t)i;
  }
  return source_idx;  // wildcard fallback, or -1 if none
}

// -- Public API --------------------------------------------------------------

void ModuleManager::reorder(const std::vector<std::string>& ids,
                            const std::string& parent_id) {
  std::lock_guard<std::recursive_mutex> lk(mu_);

  if (!parent_id.empty()) {
    // Nested reorder: reorder the children of the named parent.
    MoonModule* parent = find(parent_id.c_str());
    if (!parent) return;
    std::vector<MoonModule*> newOrder;
    for (const auto& id : ids) {
      MoonModule* m = find(id.c_str());
      if (m && m->parent_ == parent) newOrder.push_back(m);
    }
    // Append any children not listed (preserve their relative order).
    for (uint8_t i = 0; i < parent->childCount(); ++i) {
      MoonModule* c = parent->child(i);
      bool listed = false;
      for (auto* n : newOrder) if (n == c) { listed = true; break; }
      if (!listed) newOrder.push_back(c);
    }
    parent->reorderChildren(newOrder.data(),
                            static_cast<uint8_t>(newOrder.size()));
    sort_depth_first_();
    std::printf("[mm] reorder children of %s: %zu ids\n",
                parent_id.c_str(), ids.size());
    return;
  }

  // Root reorder: rearrange root-level modules.
  std::vector<std::unique_ptr<MoonModule>> out;
  out.reserve(modules_.size());
  for (const auto& id : ids) {
    for (auto& m : modules_) {
      if (m && m->id_ == id && !m->parent_) {
        out.push_back(std::move(m)); break;
      }
    }
  }
  for (auto& m : modules_) if (m && !m->parent_) out.push_back(std::move(m));
  // Re-insert non-root modules in depth-first order under their roots.
  std::vector<std::unique_ptr<MoonModule>> final_out;
  final_out.reserve(modules_.size());
  // Put the ordered roots + their subtrees first; remaining children follow.
  for (auto& root : out) {
    if (!root) final_out.push_back(std::move(root));
  }
  // Restore: move remaining (non-root) modules back and sort.
  for (auto& m : modules_) if (m) final_out.push_back(std::move(m));
  modules_ = std::move(final_out);
  sort_depth_first_();
  std::printf("[mm] reorder roots: %zu ids requested\n", ids.size());
}

bool ModuleManager::reparent(const std::string& id,
                             const std::string& new_parent_id) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  MoonModule* m = find(id.c_str());
  if (!m) { std::printf("[mm] reparent: id %s not found\n", id.c_str()); return false; }

  if (new_parent_id.empty()) {
    // Promote to root: clear the parent flag but KEEP the input value, so the
    // link survives as a plain data-flow reference (a noodle, no nesting).
    // Targeted detach only — no full-forest rebuild (that thrashes the
    // children_ arrays and fragments the ESP32 heap; Sprint 17 regression fix).
    m->parentControlIdx_ = -1;
    if (m->parent_) m->parent_->removeChild(m);
    sort_depth_first_();
    std::printf("[mm] reparent: %s → (root), parent flag cleared\n", id.c_str());
    return true;
  }

  MoonModule* new_parent = find(new_parent_id.c_str());
  if (!new_parent) {
    std::printf("[mm] reparent: new_parent_id %s not found\n", new_parent_id.c_str());
    return false;
  }

  // The parent IS an input: find the child's input named for the parent's
  // type. No such input → the drop is rejected (no universal parent).
  int8_t idx = parent_input_idx_(m, new_parent->type());
  if (idx < 0) {
    std::printf("[mm] reparent: %s has no input named '%s' — drop rejected\n",
                id.c_str(), new_parent->type());
    return false;
  }

  // Write the parent id into that input and raise its parent flag. This IS
  // the operation — nothing is copied to a separate pointer.
  ControlDescriptor& d = m->controls_[idx];
  char* buf = reinterpret_cast<char*>(d.ptr);
  size_t cap = d.maxVal > 0 ? (size_t)d.maxVal - 1 : 63;
  std::strncpy(buf, new_parent->id(), cap);
  buf[cap] = '\0';
  m->parentControlIdx_ = idx;
  m->schemaDirty_ = true;
  m->onUpdate(d.key);  // let the module re-resolve (e.g. effect re-reads layout)

  // Targeted relink: detach from old parent, attach to new. Touches only the
  // two affected children_ arrays — no full-forest teardown (Sprint 17 heap
  // regression fix; rebuild_tree_ is now only for StateStore bulk load).
  if (m->parent_ && m->parent_ != new_parent) m->parent_->removeChild(m);
  if (m->parent_ != new_parent) new_parent->addChild(m);
  sort_depth_first_();
  std::printf("[mm] reparent: %s → parent=%s via input '%s'\n",
              id.c_str(), new_parent_id.c_str(), d.key);
  return true;
}

std::vector<std::string> ModuleManager::registered_types() const {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  std::vector<std::string> out;
  out.reserve(factories_.size());
  for (const auto& kv : factories_) out.push_back(kv.first);
  return out;
}

MoonModule* ModuleManager::find(const char* id) const {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  for (const auto& m : modules_) {
    if (m->id_ == id) return m.get();
  }
  return nullptr;
}

}  // namespace pmm
