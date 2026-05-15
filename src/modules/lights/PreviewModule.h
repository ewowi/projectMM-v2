#pragma once
//
// PreviewModule — ships the current pixel frame to the frontend as a binary
// WebSocket message at 50 fps. Reads from the shared DataBuffer<RGB> declared
// in DataRegistry — zero allocation in this module (no staging buffer).
//
// Wire format (unchanged from Sprint 6):
//     byte  0:    0x02   magic / version
//     bytes 1-2:  uint16 width  (little-endian)
//     bytes 3-4:  uint16 height (LE)
//     bytes 5-6:  uint16 depth  (LE)
//     bytes 7..:  RGB[width*height*depth]
//
// Hot path: one buf try_acquire_read (two atomic loads), one broadcastBinary
// (copies into WS frame internally — unavoidable). No allocation, no staging
// buffer, no extra memcpy beyond the WS copy.
//
// broadcastBinary takes a contiguous buffer, so we allocate frame_buf_ once
// (7B header + pixel data) in setup/resolve and reuse it — same grow-once
// pattern as before but sized correctly from the registry geometry.
//

#include <cstdint>
#include <cstring>

#include "../../core/DataBuffer.h"
#include "../../core/DataRegistry.h"
#include "../../core/MoonModule.h"
#include "../../core/ModuleManager.h"
#include "../../pal/PalHeap.h"
#include "../network/WebSocketModule.h"
#include "../system/Logger.h"
#include "Pixelable.h"
#include "RGB.h"

namespace pmm {

class PreviewModule : public MoonModule {
 public:
  const char* category() const override { return "effect"; }

  void setup() override {
    resolve_buf_();
    resolve_ws_();
  }

  void onBuildControls() override {
    addControl(source_buf_, sizeof(source_buf_), "source", "text");
  }

  void onUpdate(const char* key) override {
    if (std::strcmp(key, "source") == 0) resolve_buf_();
  }

  void loop20ms() override {
    if (!ws_) resolve_ws_();
    if (!buf_) resolve_buf_();
    if (!buf_ || !ws_ || !frame_buf_) return;
    if (!ws_->hasClients()) return;
    if (!ws_->canBroadcastBinary()) return;  // queue congested — skip frame, text gets priority

    const uint32_t rev_before = buf_->revision();
    const RGB* src = buf_->try_acquire_read();
    if (!src) return;

    // Torn-frame check: with a single slot the producer may write while we
    // read. If revision changed between try_acquire_read and now, skip frame.
    if (buf_->revision() != rev_before) { buf_->release_read(); return; }

    // Header is already written in frame_buf_[0..6]; copy pixels after it.
    std::memcpy(frame_buf_ + 7, src, pixel_bytes_);
    ws_->broadcastBinary(frame_buf_, 7 + pixel_bytes_);
    buf_->release_read();
  }

  void teardown() override {
    pal::psram_free(frame_buf_);
    frame_buf_   = nullptr;
    pixel_bytes_ = 0;
    moduleAllocBytes_ = 0;
    buf_ = nullptr;
    ws_  = nullptr;
  }

 private:
  char             source_buf_[24] = "ripples-0";
  DataBuffer<RGB>* buf_            = nullptr;
  WebSocketModule* ws_             = nullptr;
  uint8_t*         frame_buf_      = nullptr;  // header + pixels; grown on resolve
  size_t           pixel_bytes_    = 0;

  void resolve_buf_() {
    buf_ = nullptr;
    const DataBufferEntry* e = DataRegistry::instance().resolve(source_buf_);
    if (!e || !e->buf_ptr) return;
    buf_ = static_cast<DataBuffer<RGB>*>(e->buf_ptr);

    // Rebuild frame_buf_ if geometry changed.
    const uint16_t w = e->dim[0], h = e->dim[1], d = e->dim[2];
    const size_t need = (size_t)w * h * d * sizeof(RGB);
    if (need != pixel_bytes_ || !frame_buf_) {
      pal::psram_free(frame_buf_);
      frame_buf_ = (uint8_t*)pal::psram_alloc(7 + need);
      if (!frame_buf_) { pixel_bytes_ = 0; moduleAllocBytes_ = 0; buf_ = nullptr; return; }
      pixel_bytes_      = need;
      moduleAllocBytes_ = 7 + need;
      // Write static header once.
      frame_buf_[0] = 0x02;
      write_u16_le_(&frame_buf_[1], w);
      write_u16_le_(&frame_buf_[3], h);
      write_u16_le_(&frame_buf_[5], d);
    }
  }

  void resolve_ws_() {
    if (!manager_) return;
    MoonModule* m = manager_->find("ws-0");
    if (!m) return;
    ws_ = static_cast<WebSocketModule*>(m);
  }

  static void write_u16_le_(uint8_t* dst, uint16_t v) {
    dst[0] = (uint8_t)(v & 0xff);
    dst[1] = (uint8_t)((v >> 8) & 0xff);
  }

 public:
  // Wire format helpers — exposed for tests; stateless so no module instance needed.
  static size_t required_frame_bytes(const PixelBufferRef& ref) {
    return 7 + (size_t)ref.count() * sizeof(RGB);
  }
  static size_t pack_frame(uint8_t* dst, const PixelBufferRef& ref) {
    dst[0] = 0x02;
    write_u16_le_(&dst[1], ref.width);
    write_u16_le_(&dst[3], ref.height);
    write_u16_le_(&dst[5], ref.depth);
    std::memcpy(dst + 7, ref.data, ref.count() * sizeof(RGB));
    return 7 + ref.count() * sizeof(RGB);
  }
};

}  // namespace pmm
