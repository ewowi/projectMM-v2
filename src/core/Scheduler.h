#pragma once

#include <atomic>

#include "MoonModule.h"
#include "ModuleManager.h"

namespace pmm {

class Scheduler {
 public:
  // `cores`: number of core_loops to run. Each module is ticked by the
  // single core whose id matches the module's `coreAffinity()`. Sprint 7
  // shipped two-core ESP32 via pal::task_create_pinned (8 KB stacks).
  explicit Scheduler(ModuleManager* mm, int cores = 2) : mm_(mm), cores_(cores) {}

  void run();
  void stop() { stop_.store(true); }

  // Public so pal::task_create_pinned's C-style entry can dispatch to it.
  void core_loop(int core_id);

 private:
  ModuleManager*    mm_;
  int               cores_;
  std::atomic<bool> stop_{false};
};

}  // namespace pmm
