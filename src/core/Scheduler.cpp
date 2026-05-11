#include "Scheduler.h"

#include <cstdio>

#include "../pal/Pal.h"

namespace pmm {

void Scheduler::run() {
  std::printf("[sched] spawning %d core thread(s)\n", cores_);
  for (int i = 0; i < cores_; ++i) {
    threads_.emplace_back([this, i] { core_loop(i); });
  }
  for (auto& t : threads_) t.join();
  std::printf("[sched] all cores joined\n");
}

void Scheduler::core_loop(int core_id) {
  std::printf("[sched] core %d: entering loop (t=%u ms)\n", core_id, pal::millis());

  uint32_t last_20ms = pal::millis();
  uint32_t last_1s = last_20ms;
  uint32_t last_10s = last_20ms;
  uint32_t ticks = 0;
  uint32_t ticks_20ms = 0;

  while (!stop_.load()) {
    uint32_t now = pal::millis();
    ticks++;

    for (size_t i = 0; i < mm_->size(); ++i) {
      if (static_cast<int>(i) % cores_ != core_id) continue;
      Module* m = mm_->at(i);
      m->loop();
      if (now - last_20ms >= 20) m->loop20ms();
      if (now - last_1s >= 1000) m->loop1s();
      if (now - last_10s >= 10000) m->loop10s();
    }

    if (now - last_20ms >= 20) { last_20ms = now; ticks_20ms++; }
    if (now - last_1s >= 1000) last_1s = now;
    if (now - last_10s >= 10000) last_10s = now;

    pal::yield();
  }

  std::printf("[sched] core %d: exiting after %u ticks (%u x 20ms cadence, t=%u ms)\n",
              core_id, ticks, ticks_20ms, pal::millis());
}

}  // namespace pmm
