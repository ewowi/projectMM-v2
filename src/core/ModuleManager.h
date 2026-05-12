#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "Module.h"

namespace pmm {

class ModuleManager {
 public:
  using Factory = std::function<std::unique_ptr<Module>()>;

  void register_type(const char* type, Factory f) { factories_[type] = std::move(f); }

  Module* add(const char* type, const char* id);
  bool remove(const char* id);
  Module* find(const char* id) const;

  // Unlocked accessors — callers must hold mutex() while using the returned pointer.
  size_t size() const { return modules_.size(); }
  Module* at(size_t i) const { return modules_[i].get(); }

  // Recursive so loop*() callbacks can call size()/at() while Scheduler holds it.
  std::recursive_mutex& mutex() { return mu_; }

 private:
  mutable std::recursive_mutex mu_;
  std::unordered_map<std::string, Factory> factories_;
  std::vector<std::unique_ptr<Module>> modules_;
};

}  // namespace pmm
