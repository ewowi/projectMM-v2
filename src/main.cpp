#include <cstdio>
#include <thread>

#include "core/ModuleManager.h"
#include "core/Scheduler.h"
#include "pal/Pal.h"

int main() {
  std::printf("[main] projectMM v2 skeleton starting (t=%u ms)\n", pal::millis());

  pmm::ModuleManager mm;
  mm.add("noop", "noop-0");
  std::printf("[main] %zu module(s) registered\n", mm.size());

  pmm::Scheduler sched(&mm, 2);
  std::printf("[main] scheduler will be stopped after 100 ms\n");

  std::thread stopper([&sched] {
    pal::sleep(100);
    std::printf("[main] stopper: requesting scheduler stop (t=%u ms)\n", pal::millis());
    sched.stop();
  });

  sched.run();
  stopper.join();

  mm.remove("noop-0");
  std::printf("[main] done: %zu module(s) remaining, %u ms elapsed\n",
              mm.size(), pal::millis());
  return 0;
}
