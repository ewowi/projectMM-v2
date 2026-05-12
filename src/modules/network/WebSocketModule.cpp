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

// Wire format matches v1's app.js dispatcher:
//   schema → {"t":"schema", "modules":[ {id,type,category,controls:[...]}, ... ]}
//   state  → [ {id, controls: {key: value, ...}}, ... ]   (raw top-level array)

void WebSocketModule::broadcast_schema_() {
  JsonDocument doc;
  doc["t"] = "schema";
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
  JsonArray arr = doc.to<JsonArray>();  // top-level array, no envelope
  {
    std::lock_guard<std::recursive_mutex> lk(manager_->mutex());
    for (size_t i = 0; i < manager_->size(); ++i) {
      JsonObject entry = arr.add<JsonObject>();
      entry["id"] = manager_->at(i)->id();
      manager_->at(i)->getControlValues(entry["controls"].to<JsonObject>());
    }
  }
  std::string out;
  out.reserve(measureJson(doc) + 1);
  serializeJson(doc, out);
  ws_->broadcastText(out);
}

}  // namespace pmm
