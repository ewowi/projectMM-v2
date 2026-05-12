#pragma once
#include <string>
#include "../../core/MoonModule.h"

namespace pmm {

class HelloModule : public MoonModule {
 public:
  void loop1s() override { if (enabled_) ++counter_; }

  void serialize_json(std::string& out) const override {
    out += "{\"type\":\"hello\",\"id\":\"" + id_ + "\""
           ",\"counter\":" + std::to_string(counter_) +
           ",\"enabled\":" + (enabled_ ? "true" : "false") + "}";
  }

  int counter_ = 0;  // enabled_ inherited from MoonModule
};

}  // namespace pmm
