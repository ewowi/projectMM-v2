#pragma once
#include <string>

namespace pmm {

class ModuleManager;

class Module {
 public:
  virtual ~Module() = default;

  virtual void setup() {}
  virtual void loop() {}
  virtual void loop20ms() {}
  virtual void loop1s() {}
  virtual void loop10s() {}
  virtual void teardown() {}

  virtual void serialize_json(std::string& out) const {
    out += "{\"type\":\"" + type_ + "\",\"id\":\"" + id_ + "\"}";
  }

  const char* id() const { return id_.c_str(); }
  const char* type() const { return type_.c_str(); }
  ModuleManager* manager() const { return manager_; }

 protected:
  std::string id_;
  std::string type_;
  ModuleManager* manager_ = nullptr;

  friend class ModuleManager;
};

}  // namespace pmm
