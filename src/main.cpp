#include <cstdio>

#include "core/ModuleManager.h"
#include "core/Scheduler.h"
#include "modules/lights/ArtnetOutModule.h"
#include "modules/lights/GridLayoutModule.h"
#include "modules/lights/PreviewModule.h"
#include "modules/lights/RipplesEffect.h"
#include "modules/lights/effects/DistortionWavesEffect.h"
#include "modules/lights/effects/FlowFluidEffect.h"
#include "modules/lights/effects/LinesEffect.h"
#include "modules/lights/effects/Noise2DEffect.h"
#include "modules/lights/effects/SineEffect.h"
#include "modules/lights/layouts/RingLayoutModule.h"
#include "modules/lights/layouts/WheelLayoutModule.h"
#include "modules/network/HttpServerModule.h"
#include "modules/network/WebSocketModule.h"
#include "modules/system/Logger.h"
#include "modules/system/StateStoreModule.h"
#include "modules/system/SystemStatusModule.h"
#include "modules/system/WifiStaModule.h"
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
  pmm::log("[main] projectMM v2 starting (t=%u ms)\n", pal::millis());

  static pmm::ModuleManager mm;
  mm.register_type<pmm::SystemStatusModule>("system");
  mm.register_type<pmm::WifiStaModule>("wifi-sta");
  mm.register_type<pmm::HttpServerModule>("http", pal::default_http_port());
  mm.register_type<pmm::WebSocketModule>("ws");
  mm.register_type<pmm::StateStoreModule>("state-store");
  mm.register_type<pmm::GridLayoutModule>("layout");
  mm.register_type<pmm::RingLayoutModule>("ring-layout");
  mm.register_type<pmm::WheelLayoutModule>("wheel-layout");
  mm.register_type<pmm::RipplesEffect>("ripples");
  mm.register_type<pmm::DistortionWavesEffect>("distortion-waves");
  mm.register_type<pmm::FlowFluidEffect>("flow-fluid");
  mm.register_type<pmm::LinesEffect>("lines");
  mm.register_type<pmm::Noise2DEffect>("noise-2d");
  mm.register_type<pmm::SineEffect>("sine");
  mm.register_type<pmm::PreviewModule>("preview");
  mm.register_type<pmm::ArtnetOutModule>("artnet-out");

  // Head modules first (always added, never persisted by state-store).
  mm.add("system",   "system-0");
  mm.add("wifi-sta", "wifi-sta-0");
  mm.add("http",     "http-0");
  mm.add("ws",       "ws-0");
  // State-store runs after the heads so its setup() can rehydrate any
  // user-added modules from /modules.json + /state/<id>.json.
  mm.add("state-store", "state-store-0");
  pmm::log("[main] %zu module(s) running  (UI on :%u, WS on :81)\n",
           mm.size(), (unsigned)pal::default_http_port());

  static pmm::Scheduler sched(&mm, pal::default_scheduler_cores());
  g_sched = &sched;
  pal::on_interrupt(on_sigint);

  sched.run();

  mm.remove("ws-0");
  mm.remove("http-0");
  mm.remove("system-0");
  pmm::log("[main] done\n");
}

void loop() {}

int main() {
  setup();
  return 0;
}
