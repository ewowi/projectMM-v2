#pragma once

#include <atomic>
#include <thread>
#include <vector>

#include "MoonModule.h"
#include "ModuleManager.h"

namespace pmm {

class Scheduler {
 public:
  explicit Scheduler(ModuleManager* mm, int cores = 2) : mm_(mm), cores_(cores) {}

  void run();
  void stop() { stop_.store(true); }

 private:
  void core_loop(int core_id);

  ModuleManager* mm_;
  int cores_;
  std::atomic<bool> stop_{false};
  std::vector<std::thread> threads_;
};

}  // namespace pmm
