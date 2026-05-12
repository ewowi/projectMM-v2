#include "ModuleManager.h"

#include <cstdio>

namespace pmm {

MoonModule* ModuleManager::add(const char* type, const char* id) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  std::unique_ptr<MoonModule> m;
  auto it = factories_.find(type);
  if (it != factories_.end()) {
    m = it->second();
  } else {
    m = std::make_unique<MoonModule>();
  }
  m->type_ = type;
  m->id_ = id;
  m->manager_ = this;
  m->setup();
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
      (*it)->teardown();
      modules_.erase(it);
      return true;
    }
  }
  std::printf("[mm] remove: id=%s not found\n", id);
  return false;
}

MoonModule* ModuleManager::find(const char* id) const {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  for (const auto& m : modules_) {
    if (m->id_ == id) return m.get();
  }
  return nullptr;
}

}  // namespace pmm
