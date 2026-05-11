#include "ModuleManager.h"

#include <cstdio>
#include <cstring>

namespace pmm {

Module* ModuleManager::add(const char* type, const char* id) {
  std::printf("[mm] add: type=%s id=%s -> setup()\n", type, id);
  auto m = std::make_unique<Module>();
  m->type_ = type;
  m->id_ = id;
  m->setup();
  Module* raw = m.get();
  modules_.push_back(std::move(m));
  return raw;
}

bool ModuleManager::remove(const char* id) {
  for (auto it = modules_.begin(); it != modules_.end(); ++it) {
    if (std::strcmp((*it)->id(), id) == 0) {
      std::printf("[mm] remove: id=%s -> teardown()\n", id);
      (*it)->teardown();
      modules_.erase(it);
      return true;
    }
  }
  std::printf("[mm] remove: id=%s not found\n", id);
  return false;
}

}  // namespace pmm
