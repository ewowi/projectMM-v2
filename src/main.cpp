#include <csignal>
#include <cstdio>
#include <memory>

#include "core/ModuleManager.h"
#include "core/Scheduler.h"
#include "modules/hello/HelloModule.h"
#include "modules/network/HttpServerModule.h"
#include "pal/Pal.h"

static pmm::Scheduler* g_sched = nullptr;
static void on_sigint(int) { if (g_sched) g_sched->stop(); }

int main() {
  std::setbuf(stdout, nullptr);  // pipe-safe: flush every write immediately
  std::printf("[main] projectMM v2 starting (t=%u ms)\n", pal::millis());

  pmm::ModuleManager mm;
  mm.register_type("hello", [] { return std::make_unique<pmm::HelloModule>(); });
  mm.register_type("http",  [] { return std::make_unique<pmm::HttpServerModule>(8080); });

  mm.add("hello", "hello-0");
  mm.add("http",  "http-0");
  std::printf("[main] %zu module(s) running  (Ctrl-C to stop, UI on :8080)\n", mm.size());

  pmm::Scheduler sched(&mm, 2);
  g_sched = &sched;
  std::signal(SIGINT, on_sigint);

  sched.run();

  mm.remove("http-0");
  mm.remove("hello-0");
  std::printf("[main] done\n");
  return 0;
}
