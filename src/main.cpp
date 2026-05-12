#include <cstdio>

#include "core/ModuleManager.h"
#include "core/Scheduler.h"
#include "modules/network/HttpServerModule.h"
#include "modules/network/WebSocketModule.h"
#include "modules/system/SystemStatusModule.h"
#include "pal/Pal.h"

// setup() owns the run loop on both platforms:
//  - PC: no Arduino framework — main() calls setup(), which blocks in
//    Scheduler::run() until SIGINT, then returns. loop() is never called.
//  - ESP32: arduino-esp32's loopTask calls setup() once. Our setup() blocks
//    in Scheduler::run() forever (Scheduler owns its own pal::task_* threads),
//    so loop() also never runs. Defining loop() satisfies the linker.
//
// No #ifdef ARDUINO here — main.cpp lives outside src/pal/, and the
// platform-guards check rejects guards there. Convergence comes from the
// shape of both entry-point contracts: define setup() + loop() + main() and
// each platform's loader picks the one that applies.

static pmm::Scheduler* g_sched = nullptr;
static void on_sigint(int) { if (g_sched) g_sched->stop(); }

void setup() {
  pal::log_init();
  std::printf("[main] projectMM v2 starting (t=%u ms)\n", pal::millis());

  static pmm::ModuleManager mm;
  mm.register_type<pmm::SystemStatusModule>("system");
  mm.register_type<pmm::HttpServerModule>("http", uint16_t{8080});
  mm.register_type<pmm::WebSocketModule>("ws");

  mm.add("system", "system-0");
  mm.add("http",   "http-0");
  mm.add("ws",     "ws-0");
  std::printf("[main] %zu module(s) running  (UI on :8080, WS on :81)\n", mm.size());

  static pmm::Scheduler sched(&mm, 2);
  g_sched = &sched;
  pal::on_interrupt(on_sigint);

  sched.run();

  mm.remove("ws-0");
  mm.remove("http-0");
  mm.remove("system-0");
  std::printf("[main] done\n");
}

void loop() {}

int main() {
  setup();
  return 0;
}
