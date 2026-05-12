#include "WebSocketModule.h"

#include <ArduinoJson.h>

#include "../../core/ModuleManager.h"

namespace pmm {

void WebSocketModule::setup() {
  ws_ = std::make_unique<pal::WsServer>();
  ws_->begin();
}

void WebSocketModule::loop20ms() {
  if (ws_) ws_->tick();  // ESP32: cleanup closed clients; PC: no-op
}

void WebSocketModule::loop1s() {
  if (!ws_ || !manager_) return;
  if (!ws_->hasClients()) return;
  broadcast_schema_();
  broadcast_state_();
}

void WebSocketModule::broadcast_schema_() {
  JsonDocument doc;
  doc["event"] = "schema";
  JsonArray arr = doc["modules"].to<JsonArray>();
  {
    std::lock_guard<std::recursive_mutex> lk(manager_->mutex());
    for (size_t i = 0; i < manager_->size(); ++i) {
      manager_->at(i)->getSchema(arr.add<JsonObject>());
    }
  }
  std::string out;
  out.reserve(measureJson(doc) + 1);
  serializeJson(doc, out);
  ws_->broadcastText(out);
}

void WebSocketModule::broadcast_state_() {
  JsonDocument doc;
  doc["event"] = "state";
  JsonArray arr = doc["modules"].to<JsonArray>();
  {
    std::lock_guard<std::recursive_mutex> lk(manager_->mutex());
    for (size_t i = 0; i < manager_->size(); ++i) {
      JsonObject m = arr.add<JsonObject>();
      m["id"] = manager_->at(i)->id();
      manager_->at(i)->getControlValues(m);
    }
  }
  std::string out;
  out.reserve(measureJson(doc) + 1);
  serializeJson(doc, out);
  ws_->broadcastText(out);
}

}  // namespace pmm
