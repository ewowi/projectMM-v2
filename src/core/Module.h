#pragma once

namespace pmm {

class Module {
 public:
  virtual ~Module() = default;

  virtual void setup() {}
  virtual void loop() {}
  virtual void loop20ms() {}
  virtual void loop1s() {}
  virtual void loop10s() {}
  virtual void teardown() {}

  const char* id() const { return id_; }
  const char* type() const { return type_; }

 protected:
  const char* id_ = "";
  const char* type_ = "";

  friend class ModuleManager;
};

}  // namespace pmm
