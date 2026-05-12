#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "MoonModule.h"

namespace pmm {

class ModuleManager {
 public:
  using Factory = std::function<std::unique_ptr<MoonModule>()>;

  // Manual factory — for cases that need a custom construction lambda.
  // Modules registered this way must set classSize_ themselves if they
  // care about footprint reporting.
  void register_type(const char* type, Factory f) { factories_[type] = std::move(f); }

  // Type-safe factory — sizeof(T) is captured at registration time and
  // applied to every instance via setClassSize(). New modules need zero
  // per-class boilerplate for footprint reporting.
  //   mm.register_type<HelloModule>("hello");
  //   mm.register_type<HttpServerModule>("http", 8080);
  template <typename T, typename... Args>
  void register_type(const char* type, Args... args) {
    auto tup = std::make_tuple(std::move(args)...);
    factories_[type] = [tup = std::move(tup)]() mutable {
      auto m = std::apply(
          [](auto&&... a) { return std::make_unique<T>(std::forward<decltype(a)>(a)...); },
          tup);
      m->setClassSize(sizeof(T));
      return std::unique_ptr<MoonModule>(std::move(m));
    };
  }

  MoonModule* add(const char* type, const char* id);
  bool remove(const char* id);
  MoonModule* find(const char* id) const;

  // Unlocked accessors — callers must hold mutex() while using the returned pointer.
  size_t size() const { return modules_.size(); }
  MoonModule* at(size_t i) const { return modules_[i].get(); }

  // Recursive so loop*() callbacks can call size()/at() while Scheduler holds it.
  std::recursive_mutex& mutex() { return mu_; }

 private:
  mutable std::recursive_mutex mu_;
  std::unordered_map<std::string, Factory> factories_;
  std::vector<std::unique_ptr<MoonModule>> modules_;
};

}  // namespace pmm
