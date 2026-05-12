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

 private:
  std::unique_ptr<pal::WsServer> ws_;

  void broadcast_schema_();
  void broadcast_state_();
};

}  // namespace pmm
