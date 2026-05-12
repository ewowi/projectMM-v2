#include "HttpServerModule.h"

#include <cstdio>
#include <mutex>

#include "../../../src/frontend/frontend_bundle.h"
#include "../../core/ModuleManager.h"

namespace pmm {

void HttpServerModule::setup() {
  server_ = std::make_unique<pal::HttpServer>(port_);

  // Serve the v1 SPA bundle (gzipped) on GET /.
  server_->onGetStaticGzip("/", "text/html",
                          FRONTEND_HTML_GZ, FRONTEND_HTML_GZ_LEN);

  // GET /api/modules — list all modules as a JSON array of their
  // serialize_json output. The manager lock is held during iteration so
  // a concurrent add/remove cannot invalidate the vector mid-list.
  server_->onGet("/api/modules", [this](const std::string&) {
    std::string body = "[";
    if (manager_) {
      std::lock_guard<std::recursive_mutex> lk(manager_->mutex());
      for (size_t i = 0; i < manager_->size(); ++i) {
        if (i > 0) body += ",";
        manager_->at(i)->serialize_json(body);
      }
    }
    body += "]";
    return pal::HttpResponse{200, "application/json", std::move(body)};
  });

  server_->begin();
  std::printf("[http] %s listening on :%u\n", id(), port_);
}

}  // namespace pmm
