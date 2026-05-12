#include "HttpServerModule.h"

#include <ArduinoJson.h>

#include <cstring>
#include <mutex>
#include <string>

#include "../../../src/frontend/frontend_bundle.h"
#include "../../core/ModuleManager.h"
#include "../system/Logger.h"

namespace pmm {

namespace {

constexpr const char* kModulesPrefix = "/api/modules/";

// Extract the {id} segment from a path like "/api/modules/hello-0".
std::string id_from_path(const std::string& path) {
  const size_t prefixLen = std::strlen(kModulesPrefix);
  if (path.size() <= prefixLen) return {};
  return path.substr(prefixLen);
}

pal::HttpResponse json_err(int status, const char* reason) {
  std::string body = std::string("{\"error\":\"") + reason + "\"}";
  return {status, "application/json", std::move(body)};
}

}  // namespace

void HttpServerModule::setup() {
  server_ = std::make_unique<pal::HttpServer>(port_);

  // GET / — serve the v1 SPA bundle (gzipped).
  server_->onGetStaticGzip("/", "text/html", FRONTEND_HTML_GZ, FRONTEND_HTML_GZ_LEN);

  // GET /api/types — registered module type names, for the frontend "Add
  // module" picker. Default category "system" until more domains land.
  server_->onGet("/api/types", [this](const std::string&) {
    std::string body = "[";
    if (manager_) {
      bool first = true;
      for (const auto& name : manager_->registered_types()) {
        if (!first) body += ",";
        first = false;
        body += "{\"name\":\"" + name + "\",\"category\":\"system\"}";
      }
    }
    body += "]";
    return pal::HttpResponse{200, "application/json", std::move(body)};
  });

  // GET /api/modules — full module schema list as JSON, same shape as the
  // WS schema event so the frontend's render() can consume either. The
  // manager lock is held during iteration so a concurrent add/remove cannot
  // invalidate the vector.
  server_->onGet("/api/modules", [this](const std::string&) {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    if (manager_) {
      std::lock_guard<std::recursive_mutex> lk(manager_->mutex());
      for (size_t i = 0; i < manager_->size(); ++i) {
        manager_->at(i)->getSchema(arr.add<JsonObject>());
      }
    }
    std::string body;
    body.reserve(measureJson(doc) + 1);
    serializeJson(doc, body);
    return pal::HttpResponse{200, "application/json", std::move(body)};
  });

  // POST /api/modules/reorder — reorder root modules. Body: {"parent_id":"",
  // "ids":["id1","id2",...]}. parent_id is reserved for nested reorder once
  // child trees exist (Sprint 6); ignored here. Registered before the more
  // general "/api/modules" POST so exact-path matching cannot clash.
  server_->onPost("/api/modules/reorder", [this](const std::string& body) {
    if (!manager_) return json_err(500, "no manager");
    JsonDocument doc;
    if (deserializeJson(doc, body)) return json_err(400, "bad json");
    std::vector<std::string> ids;
    for (JsonVariantConst v : doc["ids"].as<JsonArrayConst>()) {
      const char* s = v.as<const char*>();
      if (s) ids.emplace_back(s);
    }
    manager_->reorder(ids);
    return pal::HttpResponse{200, "application/json", "{\"ok\":true}"};
  });

  // POST /api/modules — add a module. Body: {"type":"<t>","id":"<i>"}.
  server_->onPost("/api/modules", [this](const std::string& body) {
    if (!manager_) return json_err(500, "no manager");
    JsonDocument doc;
    if (deserializeJson(doc, body)) return json_err(400, "bad json");
    const char* type = doc["type"] | "";
    const char* id   = doc["id"]   | "";
    if (!*type || !*id) return json_err(400, "type and id required");

    std::lock_guard<std::recursive_mutex> lk(manager_->mutex());
    if (manager_->find(id)) return json_err(409, "id exists");
    if (!manager_->add(type, id)) return json_err(404, "unknown type");
    return pal::HttpResponse{201, "application/json", "{\"ok\":true}"};
  });

  // DELETE /api/modules/<id> — remove a module.
  server_->onDelete("/api/modules/.+", [this](const std::string& path) {
    if (!manager_) return json_err(500, "no manager");
    std::string id = id_from_path(path);
    if (id.empty()) return json_err(400, "id required");
    if (manager_->remove(id.c_str())) {
      return pal::HttpResponse{200, "application/json", "{\"ok\":true}"};
    }
    return json_err(404, "not found");
  });

  // GET /api/system — aggregate runtime metrics. Each module contributes via
  // its MoonModule::fillSystemJson override (SystemStatusModule fills heap,
  // PSRAM, fs, chip, etc.; future modules add their own fields).
  server_->onGet("/api/system", [this](const std::string&) {
    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();
    if (manager_) {
      std::lock_guard<std::recursive_mutex> lk(manager_->mutex());
      for (size_t i = 0; i < manager_->size(); ++i) manager_->at(i)->fillSystemJson(root);
    }
    std::string body;
    body.reserve(measureJson(doc) + 1);
    serializeJson(doc, body);
    return pal::HttpResponse{200, "application/json", std::move(body)};
  });

  // POST /api/control — single-control update from the frontend's `input`
  // handler. Body: {"id":"<module-id>","key":"<control>","value":<v>}.
  // The frontend was ported verbatim from v1, which uses this endpoint;
  // we expose it here to honour the v1 wire contract. PATCH /api/modules/{id}
  // is available too — same setControl() flow under the hood — for callers
  // who prefer a REST-shaped path.
  server_->onPost("/api/control", [this](const std::string& body) {
    if (!manager_) return json_err(500, "no manager");
    JsonDocument doc;
    if (deserializeJson(doc, body)) return json_err(400, "bad json");
    const char* id  = doc["id"]  | "";
    const char* key = doc["key"] | "";
    if (!*id || !*key) return json_err(400, "id and key required");
    std::lock_guard<std::recursive_mutex> lk(manager_->mutex());
    MoonModule* m = manager_->find(id);
    if (!m) return json_err(404, "not found");
    m->setControl(key, JsonVariantConst(doc["value"]));
    return pal::HttpResponse{200, "application/json", "{\"ok\":true}"};
  });

  // GET /api/log — recent log lines from pmm::Logger's ring buffer. Format
  // matches the frontend's app.js poller: {"entries":["line",...]}.
  server_->onGet("/api/log", [](const std::string&) {
    JsonDocument doc;
    JsonArray arr = doc["entries"].to<JsonArray>();
    for (const auto& line : pmm::log_buffer()) arr.add(line);
    std::string body;
    body.reserve(measureJson(doc) + 1);
    serializeJson(doc, body);
    return pal::HttpResponse{200, "application/json", std::move(body)};
  });

  // PATCH /api/modules/<id> — set one or more controls on a module.
  // Body: {"<key>": <value>, ...}. Each key invokes setControl(); unknown keys
  // are silently ignored (matches v1 behaviour — the schema is the contract).
  server_->onPatch("/api/modules/.+", [this](const std::string& path, const std::string& body) {
    if (!manager_) return json_err(500, "no manager");
    std::string id = id_from_path(path);
    if (id.empty()) return json_err(400, "id required");
    JsonDocument doc;
    if (deserializeJson(doc, body)) return json_err(400, "bad json");

    std::lock_guard<std::recursive_mutex> lk(manager_->mutex());
    MoonModule* m = manager_->find(id.c_str());
    if (!m) return json_err(404, "not found");
    for (JsonPairConst kv : doc.as<JsonObjectConst>()) {
      m->setControl(kv.key().c_str(), kv.value());
    }
    return pal::HttpResponse{200, "application/json", "{\"ok\":true}"};
  });

  server_->begin();
}

}  // namespace pmm
