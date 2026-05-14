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
  // Retry begin() in case the network came up after setup() (ESP32 case).
  // Idempotent; cheap once started.
  ws_->begin();
  if (!ws_->hasClients()) return;

  // Broadcast schema only when something actually changed. The frontend
  // tears down the DOM on every schema event (render() rebuilds every
  // module card from scratch), so re-sending it every second would steal
  // input focus and reset the preview canvas — both observed bugs.
  // Triggers: extra client connected, modules added/removed (size
  // changed), or any module flagged its schema dirty.
  bool need_schema = false;
  const size_t clients = ws_->clientCount();
  if (clients > last_client_count_) need_schema = true;
  last_client_count_ = clients;
  {
    std::lock_guard<std::recursive_mutex> lk(manager_->mutex());
    const size_t n = manager_->size();
    if (n != last_module_count_) need_schema = true;
    last_module_count_ = n;
    for (size_t i = 0; i < n; ++i) {
      if (manager_->at(i)->schemaDirty()) { need_schema = true; break; }
    }
  }

  if (need_schema) {
    broadcast_schema_();
    std::lock_guard<std::recursive_mutex> lk(manager_->mutex());
    for (size_t i = 0; i < manager_->size(); ++i) {
      manager_->at(i)->clearSchemaDirty();
    }
  }
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
      MoonModule* m = manager_->at(i);
      entry["id"] = m->id();
      // Per-module timing — frontend's fps/ms toggle reads ms_per_tick.
      // self_ms_per_tick will diverge from ms_per_tick once Sprint 6 adds
      // child-recursion accounting; for now they're the same value.
      JsonObject t = entry["timing"].to<JsonObject>();
      t["us_per_tick"]      = m->usPerTick();
      t["self_us_per_tick"] = m->usPerTick();
      m->getControlValues(entry["controls"].to<JsonObject>());
    }
  }
  std::string out;
  out.reserve(measureJson(doc) + 1);
  serializeJson(doc, out);
  ws_->broadcastText(out);
}

}  // namespace pmm
