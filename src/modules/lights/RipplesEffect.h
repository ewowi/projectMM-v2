#pragma once
//
// RipplesEffect — radial sine ripples on a 2D panel (with optional repeated
// depth slices). Sprint 6 frugal: writes its own RGB buffer in loop20ms,
// publishes itself to PixelRegistry so PreviewModule and ArtnetOutModule
// can pick up the frame. No SPSC ring, no PSRAM yet — those land in Sprint 7
// when the dimension ceiling rises from 64 to 128.
//
// Controls:
//   width, height: 1..64 (default 16) — buffer geometry
//   depth: 1..16 (default 1) — repeats the 2D pattern per slice (placeholder
//          for true 3D ripple propagation, which is a future sprint)
//   speed: float, default 1.0 — radians/sec the wave advances
//   hue_base: float 0..1, default 0.6 — base hue (rotates with distance)
//
// Allocation: `pixels_ = new RGB[w*h*d]` in onAllocateMemory. On any
// dimension change, onUpdate reallocates and bumps revision_++. Buffer
// freed in teardown.
//

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../../core/MoonModule.h"
#include "../../pal/Pal.h"
#include "../system/Logger.h"
#include "Pixelable.h"
#include "PixelRegistry.h"
#include "RGB.h"

namespace pmm {

class RipplesEffect : public MoonModule, public PixelSource {
 public:
  const char* category() const override { return "effect"; }

  void setup() override {
    PixelRegistry::instance().publish(id(), this);
  }

  void onBuildControls() override {
    addControl(width_,    "width",    "slider",  1.0f, 64.0f);
    addControl(height_,   "height",   "slider",  1.0f, 64.0f);
    addControl(depth_,    "depth",    "slider",  1.0f, 16.0f);
    addControl(speed_,    "speed",    "slider",  0.1f, 10.0f);
    addControl(hue_base_, "hue_base", "slider",  0.0f, 1.0f);
  }

  void onAllocateMemory() override {
    allocate_();
  }

  void onUpdate(const char* key) override {
    // Any geometry change reallocates the buffer + bumps revision so
    // consumers re-derive count-dependent state (frontend redraws the
    // canvas at the new size, Art-Net recomputes universe count).
    if (std::strcmp(key, "width")  == 0 ||
        std::strcmp(key, "height") == 0 ||
        std::strcmp(key, "depth")  == 0) {
      allocate_();
    }
  }

  void loop20ms() override {
    if (!pixels_) return;
    const uint32_t now_ms = pal::millis();
    const float t = (float)now_ms / 1000.0f * speed_;
    const float cx = (float)width_  * 0.5f - 0.5f;
    const float cy = (float)height_ * 0.5f - 0.5f;
    const uint16_t w = (uint16_t)width_;
    const uint16_t h = (uint16_t)height_;
    const uint16_t d = (uint16_t)depth_;

    for (uint16_t z = 0; z < d; ++z) {
      for (uint16_t y = 0; y < h; ++y) {
        const float dy = (float)y - cy;
        for (uint16_t x = 0; x < w; ++x) {
          const float dx = (float)x - cx;
          const float dist = std::sqrt(dx * dx + dy * dy);
          // Wave: cos(distance - time) in [-1, 1] → brightness in [0, 1]
          const float wave = 0.5f + 0.5f * std::cos(dist * 0.6f - t * 2.0f);
          // Hue rotates with distance so the ripples have visible bands
          const float hue = hue_base_ + dist * 0.05f;
          pixels_[((uint32_t)z * h + y) * w + x] = RGB::fromHsv(hue, 1.0f, wave);
        }
      }
    }
  }

  void teardown() override {
    PixelRegistry::instance().unpublish(this);
    if (pixels_) { delete[] pixels_; pixels_ = nullptr; }
  }

  PixelBufferRef pixelBuffer() const override {
    return { pixels_,
             (uint16_t)width_,
             (uint16_t)height_,
             (uint16_t)depth_,
             revision_ };
  }

 private:
  uint32_t width_    = 16;
  uint32_t height_   = 16;
  uint32_t depth_    = 1;
  float    speed_    = 1.0f;
  float    hue_base_ = 0.6f;

  RGB*     pixels_   = nullptr;
  uint32_t revision_ = 0;
  uint32_t allocated_count_ = 0;

  void allocate_() {
    const uint32_t want = (uint32_t)width_ * (uint32_t)height_ * (uint32_t)depth_;
    if (want == allocated_count_ && pixels_) return;  // no-op on same-size rebuild
    if (pixels_) { delete[] pixels_; pixels_ = nullptr; }
    if (want == 0) { allocated_count_ = 0; ++revision_; moduleAllocBytes_ = 0; return; }
    pixels_ = new (std::nothrow) RGB[want];
    if (!pixels_) {
      log("[ripples] alloc failed at %ux%ux%u (%u bytes)\n",
          (unsigned)width_, (unsigned)height_, (unsigned)depth_, (unsigned)(want * 3));
      allocated_count_ = 0;
      moduleAllocBytes_ = 0;
      ++revision_;
      return;
    }
    std::memset(pixels_, 0, want * sizeof(RGB));
    allocated_count_ = want;
    moduleAllocBytes_ = want * sizeof(RGB);
    ++revision_;
    log("[ripples] allocated %ux%ux%u = %u bytes\n",
        (unsigned)width_, (unsigned)height_, (unsigned)depth_, (unsigned)(want * 3));
  }
};

}  // namespace pmm
