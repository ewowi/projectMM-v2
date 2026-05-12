#pragma once
//
// ArtnetOutModule — packs the current pixel frame into Art-Net OpDmx
// packets and sends them via UDP. Multi-universe: each Art-Net universe
// carries 510 channels (= 170 RGB pixels), so a 64×64 panel = 4096 pixels
// = 25 universes per frame. At 50 fps, 1 250 packets/sec ≈ 6 Mbps —
// comfortable on 2.4 GHz WiFi for both esp32dev and esp32s3_n16r8.
//
// Wire format (one packet per universe, 18-byte header + DMX bytes):
//
//     "Art-Net\0"          8 bytes  ID
//     0x00 0x50            2 bytes  OpCode (OpDmx, little-endian)
//     0x00 0x0e            2 bytes  ProtVer (14, big-endian per spec)
//     seq                  1 byte   Sequence (0 = ignore)
//     phys                 1 byte   Physical port (0)
//     uni_lo uni_hi        2 bytes  Universe (little-endian)
//     len_hi len_lo        2 bytes  DMX byte count (big-endian)
//     <data>               1..510 bytes
//
// Same-core consumer in Sprint 6: reads `pixelBuffer()` directly via the
// registry. Sprint 7 moves this to core 1 and reads the FrameRing instead.
//

#include <cstdint>
#include <cstring>

#include "../../core/MoonModule.h"
#include "../../pal/PalUdp.h"
#include "../system/Logger.h"
#include "FrameRing.h"
#include "Pixelable.h"
#include "PixelRegistry.h"
#include "RGB.h"

namespace pmm {

class ArtnetOutModule : public MoonModule {
 public:
  ArtnetOutModule() { setCoreAffinity(1); }  // Sprint 7 — run on core 1 (APP_CPU on ESP32)

  const char* category() const override { return "driver"; }

  void setup() override {
    udp_.begin();
    resolve_source_();
  }

  void onBuildControls() override {
    addControl(source_buf_,   sizeof(source_buf_),   "source",   "text");
    addControl(dest_ip_buf_,  sizeof(dest_ip_buf_),  "dest_ip",  "text");
    addControl(universe_,                            "universe", "range", 0.0f, 15.0f);
  }

  void onUpdate(const char* key) override {
    if (std::strcmp(key, "source") == 0) resolve_source_();
  }

  void loop20ms() override {
    if (!source_) resolve_source_();
    if (!source_) return;

    // Sprint 7 cross-core path: read the most-recently-published frame
    // from the producer's depth-2 SPSC ring. Memory ordering pairs with
    // the producer's release-store so pixel data is fully visible here
    // before we send.
    FrameRing* ring = source_->frameRing();
    const uint8_t* bytes = nullptr;
    uint32_t total_bytes = 0;
    bool released_after = false;
    if (ring) {
      const RGB* frame = ring->try_acquire_read();
      if (!frame) return;  // no fresh frame since last tick
      bytes = reinterpret_cast<const uint8_t*>(frame);
      total_bytes = (uint32_t)ring->slot_bytes();
      released_after = true;
    } else {
      // Fallback for sources without a ring (PC tests of effects that
      // skip the cross-core path). Reads pixelBuffer() directly.
      PixelBufferRef ref = source_->pixelBuffer();
      if (!ref.valid()) return;
      bytes = reinterpret_cast<const uint8_t*>(ref.data);
      total_bytes = ref.count() * sizeof(RGB);
    }

    uint32_t offset = 0;
    uint8_t  uni    = (uint8_t)universe_;
    while (offset < total_bytes) {
      const uint32_t remaining = total_bytes - offset;
      const uint16_t chunk     = remaining > kDmxPerPacket
                                 ? (uint16_t)kDmxPerPacket
                                 : (uint16_t)remaining;
      pack_header(packet_, uni, chunk);
      std::memcpy(packet_ + kHeaderBytes, bytes + offset, chunk);
      udp_.send(dest_ip_buf_, kArtnetPort, packet_, kHeaderBytes + chunk);
      offset += chunk;
      ++uni;
    }

    if (released_after) ring->release_read();
  }

 public:
  static constexpr uint16_t kArtnetPort    = 6454;
  static constexpr uint16_t kDmxPerPacket  = 510;   // 170 RGB pixels
  static constexpr uint16_t kHeaderBytes   = 18;

  // Fill the 18-byte Art-Net OpDmx header at `dst[0..17]`. Exposed static so
  // tests can assert the wire bytes without sending UDP. The caller copies
  // up to `dmx_len` payload bytes into `dst[18..]`.
  static void pack_header(uint8_t* dst, uint8_t universe, uint16_t dmx_len) {
    static constexpr uint8_t kArtNetId[8] = {'A','r','t','-','N','e','t',0};
    std::memcpy(dst, kArtNetId, 8);
    dst[8]  = 0x00; dst[9]  = 0x50;       // OpDmx, little-endian
    dst[10] = 0x00; dst[11] = 0x0e;       // ProtVer 14, big-endian
    dst[12] = 0;                          // sequence (0 = disabled)
    dst[13] = 0;                          // physical port
    dst[14] = universe;                   // universe lo
    dst[15] = 0;                          // universe hi (net+subnet stay 0 for Sprint 6)
    dst[16] = (uint8_t)(dmx_len >> 8);    // length hi (big-endian)
    dst[17] = (uint8_t)(dmx_len & 0xff);  // length lo
  }

 private:
  char       source_buf_[24]  = "ripples-0";
  char       dest_ip_buf_[24] = "255.255.255.255";
  uint32_t   universe_        = 0;
  PixelSource* source_        = nullptr;
  pal::Udp   udp_;
  // One packet buffer reused per universe: 18-byte header + max 510 bytes.
  uint8_t    packet_[kHeaderBytes + kDmxPerPacket] = {};

  void resolve_source_() {
    source_ = PixelRegistry::instance().find(source_buf_);
  }
};

}  // namespace pmm
