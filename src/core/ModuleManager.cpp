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
    m = std::make_unique<MoonModule>();
    m->type_ = "unknown";
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

void ModuleManager::reorder(const std::vector<std::string>& ids) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  std::vector<std::unique_ptr<MoonModule>> out;
  out.reserve(modules_.size());
  for (const auto& id : ids) {
    for (auto it = modules_.begin(); it != modules_.end(); ++it) {
      if (*it && (*it)->id_ == id) {
        out.push_back(std::move(*it));
        modules_.erase(it);
        break;
      }
    }
  }
  for (auto& m : modules_) {
    if (m) out.push_back(std::move(m));
  }
  modules_ = std::move(out);
  std::printf("[mm] reorder: %zu ids requested\n", ids.size());
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
