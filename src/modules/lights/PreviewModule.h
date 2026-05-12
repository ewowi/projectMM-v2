#pragma once
//
// PreviewModule — ships the current pixel frame to the frontend as a binary
// WebSocket message at 50 fps. Same-core consumer (Sprint 6): reads the
// PixelSource's buffer directly via the registry cache. Hot path = one
// virtual call + one revision compare + a memcpy into the frame buffer.
//
// Wire format matches the frontend's renderPixelFrame in app.js:
//
//     byte  0:    0x02   magic / version
//     bytes 1-2:  uint16 width  (little-endian)
//     bytes 3-4:  uint16 height (LE)
//     bytes 5-6:  uint16 depth  (LE)
//     bytes 7..:  RGB[width*height*depth]
//
// The 7-byte header is fixed; the body grows with the buffer geometry
// (768 B at 16×16×1, 12 288 B at the 64×64×1 ceiling for Sprint 6).
//

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "../../core/MoonModule.h"
#include "../../core/ModuleManager.h"
#include "../network/WebSocketModule.h"
#include "../system/Logger.h"
#include "Pixelable.h"
#include "PixelRegistry.h"
#include "RGB.h"

namespace pmm {

class PreviewModule : public MoonModule {
 public:
  const char* category() const override { return "effect"; }

  void setup() override {
    resolve_source_();
    resolve_ws_();
  }

  void onBuildControls() override {
    addControl(source_buf_, sizeof(source_buf_), "source", "text");
  }

  void onUpdate(const char* key) override {
    if (std::strcmp(key, "source") == 0) resolve_source_();
  }

  void loop20ms() override {
    if (!ws_) resolve_ws_();
    if (!source_) resolve_source_();          // tolerate late-add producers
    if (!source_ || !ws_) return;
    if (!ws_->hasClients()) return;           // skip the memcpy when no one is watching

    const PixelBufferRef ref = source_->pixelBuffer();
    if (!ref.valid()) return;

    const size_t frame_bytes = required_frame_bytes(ref);
    if (frame_.size() < frame_bytes) frame_.resize(frame_bytes);
    pack_frame(frame_.data(), ref);
    ws_->broadcastBinary(frame_.data(), frame_bytes);
  }

  // Wire-format packer, exposed static so tests can assert the bytes without
  // spinning up a WebSocket server. `dst` must have room for at least
  // `required_frame_bytes(ref)`. Returns the same byte count.
  static size_t required_frame_bytes(const PixelBufferRef& ref) {
    return 7 + ref.count() * sizeof(RGB);
  }
  static size_t pack_frame(uint8_t* dst, const PixelBufferRef& ref) {
    dst[0] = 0x02;
    write_u16_le_(&dst[1], ref.width);
    write_u16_le_(&dst[3], ref.height);
    write_u16_le_(&dst[5], ref.depth);
    std::memcpy(&dst[7], ref.data, ref.count() * sizeof(RGB));
    return required_frame_bytes(ref);
  }

  void teardown() override {
    source_ = nullptr;
    ws_     = nullptr;
  }

 private:
  char              source_buf_[24] = "ripples-0";
  PixelSource*      source_ = nullptr;
  WebSocketModule*  ws_     = nullptr;
  std::vector<uint8_t> frame_;  // grown once on first frame; reused thereafter

  void resolve_source_() {
    source_ = PixelRegistry::instance().find(source_buf_);
  }

  void resolve_ws_() {
    if (!manager_) return;
    MoonModule* m = manager_->find("ws-0");
    if (!m) return;
    // Static cast is safe because the type() string identifies the concrete
    // class. arduino-esp32 builds with -fno-rtti so dynamic_cast is unavailable.
    if (std::strcmp(m->type(), "ws") == 0) {
      ws_ = static_cast<WebSocketModule*>(m);
    }
  }

  static void write_u16_le_(uint8_t* dst, uint16_t v) {
    dst[0] = (uint8_t)(v & 0xff);
    dst[1] = (uint8_t)((v >> 8) & 0xff);
  }
};

}  // namespace pmm
