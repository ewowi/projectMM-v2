#pragma once
#include <cstdint>
#include <memory>

#include "../../core/MoonModule.h"
#include "../../pal/PalHttp.h"

namespace pmm {

// HttpServerModule — serves the frontend bundle on GET / and lists the
// module manager's contents on GET /api/modules. Wraps pal::HttpServer,
// which holds all platform conditionals (cpp-httplib on PC, ESPAsyncWebServer
// on ESP32); this file is platform-neutral.
//
// Sprint 2: list-only. Mutation routes (POST/DELETE/PATCH) land in Sprint 3
// when the control system arrives.
class HttpServerModule : public MoonModule {
 public:
  explicit HttpServerModule(uint16_t port = 8080) : port_(port) {}

  void setup() override;
  // loop1s(): pal::HttpServer::begin() is idempotent and waits for the
  // network to come up before binding (ESP32 needs WiFi for lwIP). Calling
  // it once a second lets the listener start automatically when WifiStaModule
  // brings up the netif. After it succeeds, subsequent calls return immediately.
  void loop1s() override { if (server_) server_->begin(); }
  // teardown: pal::HttpServer's destructor stops the listener and joins
  //           its thread; nothing extra needed here.

 private:
  uint16_t port_;
  // Allocated in setup() (not in the constructor) so module-init timing
  // matches the v2 lifecycle contract: resources come up at setup() time.
  std::unique_ptr<pal::HttpServer> server_;
};

}  // namespace pmm
