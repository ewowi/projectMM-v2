#pragma once

#include <memory>
#include <vector>

#include "Module.h"

namespace pmm {

class ModuleManager {
 public:
  Module* add(const char* type, const char* id);
  bool remove(const char* id);

  size_t size() const { return modules_.size(); }
  Module* at(size_t i) const { return modules_[i].get(); }

 private:
  std::vector<std::unique_ptr<Module>> modules_;
};

}  // namespace pmm
