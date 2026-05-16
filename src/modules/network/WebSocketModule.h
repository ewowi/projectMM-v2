#pragma once
#include <memory>

#include "../../core/MoonModule.h"
#include "../../pal/PalWs.h"

namespace pmm {

// WebSocketModule — broadcasts module schema + control values over a
// pal::WsServer (port 81). Platform-neutral: no #ifdefs; the platform
// branch lives in pal/PalWs.h.
//
// Sprint 3: 1Hz schema + state push from loop1s. Frontend connects on
// the websocket port, receives JSON frames:
//   {"event":"schema","modules":[{id,type,category,controls:[...]},...]}
//   {"event":"state", "modules":[{id, <key>: <value>, ...},...]}
//
// Sprint 4+ may add schemaDirty-driven early push and binary frames for
// the lighting preview pipeline.
class WebSocketModule : public MoonModule {
 public:
  void setup() override;
  void loop20ms() override;  // ESP32: cleanup closed clients; PC: no-op
  void loop1s() override;    // broadcast schema + state if hasClients

  // Push a raw binary frame to all connected clients. Used by Sprint 6's
  // PreviewModule for the preview-frame pixel blob; callers handle their
  // own framing (e.g. the [0x02, w, h, d, RGB...] header the frontend's
  // renderPixelFrame parses).
  void broadcastBinary(const uint8_t* data, size_t len) {
    if (ws_) ws_->broadcastBinary(data, len);
  }
  bool hasClients()        const { return ws_ && ws_->hasClients(); }
  bool canBroadcastBinary() { return ws_ && ws_->canBroadcastBinary(); }

 private:
  std::unique_ptr<pal::WsServer> ws_;
  // Tracked across loop1s ticks so schema only re-broadcasts when something
  // actually changed (new client, module added/removed, schemaDirty bumped).
  size_t last_client_count_ = 0;
  size_t last_module_count_ = 0;

  void broadcast_schema_();
  void broadcast_state_();
};

}  // namespace pmm
