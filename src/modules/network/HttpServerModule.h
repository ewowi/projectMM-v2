#pragma once
#include <cstdint>
#include <memory>

#include "../../core/Module.h"
#include "../../pal/PalHttp.h"

namespace pmm {

// HttpServerModule — serves the frontend bundle on GET / and lists the
// module manager's contents on GET /api/modules. Wraps pal::HttpServer,
// which holds all platform conditionals (cpp-httplib on PC, ESPAsyncWebServer
// on ESP32); this file is platform-neutral.
//
// Sprint 2: list-only. Mutation routes (POST/DELETE/PATCH) land in Sprint 3
// when the control system arrives.
class HttpServerModule : public Module {
 public:
  explicit HttpServerModule(uint16_t port = 8080) : port_(port) {}

  void setup() override;
  // teardown: pal::HttpServer's destructor stops the listener and joins
  //           its thread; nothing extra needed here.

 private:
  uint16_t port_;
  // Allocated in setup() (not in the constructor) so module-init timing
  // matches the v2 lifecycle contract: resources come up at setup() time.
  std::unique_ptr<pal::HttpServer> server_;
};

}  // namespace pmm
