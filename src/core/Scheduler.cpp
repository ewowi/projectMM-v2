#include "Scheduler.h"

#include <cstdio>

#include "../pal/Pal.h"
#include "../pal/PalRtos.h"

namespace pmm {

namespace {

// Per-core task entry argument. Stored as plain static so it outlives the
// task (Scheduler::run never returns on ESP32; on PC the std::thread is
// detached and lives for the process). Up to 4 cores covers PRO/APP on
// classic ESP32, both on S3, and 1..4 logical threads on PC.
struct CoreArg { Scheduler* sched; int core_id; };
static CoreArg s_core_args[4];

void core_task_entry(void* arg) {
  auto* a = static_cast<CoreArg*>(arg);
  a->sched->core_loop(a->core_id);
}

}  // namespace

void Scheduler::run() {
  std::printf("[sched] starting %d core(s) (1 inline + %d task%s)\n",
              cores_, cores_ - 1, cores_ - 1 == 1 ? "" : "s");
  // Spawn (cores_ - 1) extra cores via pal::task_create_pinned. Sprint 5
  // tried std::thread and the ~3 KB pthread default stack on arduino-esp32
  // crashed on the first tick — Sprint 7 routes through PalRtos with 8 KB
  // stacks pinned to the requested core. PC uses std::thread under the
  // pal hood (core_id ignored, OS scheduler decides).
  for (int i = 1; i < cores_ && i < (int)(sizeof(s_core_args) / sizeof(s_core_args[0])); ++i) {
    s_core_args[i] = {this, i};
    pal::task_create_pinned(core_task_entry, "pmm-core", 8 * 1024,
                            &s_core_args[i], /*priority=*/1, /*core=*/i);
  }
  core_loop(0);
  std::printf("[sched] core 0 exit (other cores keep running until process exit)\n");
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

    {
      std::lock_guard<std::recursive_mutex> lk(mm_->mutex());
      for (size_t i = 0; i < mm_->size(); ++i) {
        MoonModule* m = mm_->at(i);
        if (m->coreAffinity() != core_id) continue;
        // Use dispatch wrappers so child recursion + enabled_ gating are honored.
        m->runLoop();
        if (now - last_20ms >= 20) m->runLoop20ms();
        if (now - last_1s >= 1000) m->runLoop1s();
        if (now - last_10s >= 10000) m->runLoop10s();
      }
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
