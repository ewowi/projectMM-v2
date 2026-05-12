#pragma once
//
// RipplesEffect — radial sine ripples on a 2D panel (with optional repeated
// depth slices). Sprint 6 minimal: writes its own RGB buffer in loop20ms,
// publishes itself to PixelRegistry so PreviewModule and ArtnetOutModule
// can pick up the frame.
//
// Controls:
//   width, height: 1..128 (default 16) — buffer geometry
//   depth: 1..16 (default 1) — repeats the 2D pattern per slice (placeholder
//          for true 3D ripple propagation, which is a future sprint)
//   speed: float, default 1.0 — radians/sec the wave advances
//   hue_base: float 0..1, default 0.6 — base hue (rotates with distance)
//
// Allocation: `pixels_ = new RGB[w*h*d]` in onAllocateMemory. On any
// dimension change, onUpdate reallocates and bumps revision_++. Buffer
// freed in teardown.
//
// Hot-path optimisation: the inner per-pixel work used to be
// `sqrt + cos + HSV→RGB`, all software floating point — about 250 ms/frame
// at 128×128 on esp32-s3. We now precompute two w*h tables at geometry/
// hue_base change time:
//   * `phase_offset_`: per-pixel `dist * 0.6` as Q16 phase (uint16_t).
//   * `base_color_`: per-pixel full-bright HSV→RGB at hue=hue_base+dist·0.05.
// Per frame the inner loop is one Q16 subtract (wraps naturally), one 256-
// entry LUT load for the cos-based brightness, and three uint8 multiply-
// shifts. No sqrt, no cos, no HSV per pixel. Speed control is honoured by
// the per-frame `t_q` scalar; hue_base by the table rebuild.
//

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../../core/MoonModule.h"
#include "../../pal/Pal.h"
#include "../../pal/PalHeap.h"
#include "../system/Logger.h"
#include "FrameRing.h"
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
    addControl(width_,    "width",    "slider",  1.0f, 128.0f);
    addControl(height_,   "height",   "slider",  1.0f, 128.0f);
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
    } else if (std::strcmp(key, "hue_base") == 0) {
      rebuild_color_table_();  // phase table is unaffected; just recolour
    }
  }

  void loop20ms() override {
    if (!pixels_ || !phase_offset_ || !base_color_) return;
    const uint8_t* const bri_lut = cos_bri_lut_();
    const uint16_t w = (uint16_t)width_;
    const uint16_t h = (uint16_t)height_;
    const uint16_t d = (uint16_t)depth_;
    const uint32_t plane = (uint32_t)w * h;

    // Per-frame phase scalar: `t * speed * 2` rad, wrapped to Q16. fmod
    // before the cast keeps the float in range as uptime grows. One float
    // op per frame — negligible vs the per-pixel loop.
    const float arg = (float)pal::millis() * 0.001f * speed_ * 2.0f;
    const float wrapped = std::fmod(arg, 2.0f * 3.14159265358979f);
    const uint16_t t_q = (uint16_t)(int32_t)(wrapped * (65536.0f / (2.0f * 3.14159265358979f)));

    for (uint16_t z = 0; z < d; ++z) {
      RGB* out = pixels_ + (uint32_t)z * plane;
      for (uint32_t i = 0; i < plane; ++i) {
        const uint16_t phase = (uint16_t)(phase_offset_[i] - t_q);
        const uint8_t  bri   = bri_lut[phase >> 8];
        const RGB      c     = base_color_[i];
        out[i].r = (uint8_t)((uint16_t)c.r * bri >> 8);
        out[i].g = (uint8_t)((uint16_t)c.g * bri >> 8);
        out[i].b = (uint8_t)((uint16_t)c.b * bri >> 8);
      }
    }
    // Publish a copy into the cross-core SPSC ring for Art-Net out
    // (Sprint 7, pinned to core 1). Same-core consumers (Preview) bypass
    // the ring and read `pixels_` directly through pixelBuffer().
    if (ring_.valid()) {
      RGB* dst = ring_.acquire_write_slot();
      std::memcpy(dst, pixels_, allocated_count_ * sizeof(RGB));
      ring_.publish();
    }
  }

  void teardown() override {
    PixelRegistry::instance().unpublish(this);
    free_pixels_();
  }

  PixelBufferRef pixelBuffer() const override {
    return { pixels_,
             (uint16_t)width_,
             (uint16_t)height_,
             (uint16_t)depth_,
             revision_ };
  }

  FrameRing* frameRing() override {
    return ring_.valid() ? &ring_ : nullptr;
  }

 private:
  uint32_t width_    = 16;
  uint32_t height_   = 16;
  uint32_t depth_    = 1;
  float    speed_    = 1.0f;
  float    hue_base_ = 0.6f;

  RGB*      pixels_   = nullptr;
  uint32_t  revision_ = 0;
  uint32_t  allocated_count_ = 0;
  FrameRing ring_;  // depth-2 SPSC for cross-core consumers (Sprint 7)

  // Per-pixel precomputed tables, sized w*h (one plane — depth slices
  // share). Static between geometry/hue_base changes; rebuilt by allocate_
  // and rebuild_color_table_.
  uint16_t* phase_offset_ = nullptr;  // Q16: dist * 0.6 in cycles*65536
  RGB*      base_color_   = nullptr;  // fromHsv(hue_base + dist·0.05, 1, 1)
  uint32_t  plane_count_  = 0;        // w * h

  // 256-entry cos brightness LUT: `(int)(127.5 + 127.5 * cos(2π·i/256))`.
  // Function-static so the table is shared across instances and built once
  // on first call (C++11 guarantees thread-safe init).
  static const uint8_t* cos_bri_lut_() {
    static uint8_t lut[256];
    static bool    init = false;
    if (!init) {
      for (int i = 0; i < 256; ++i) {
        const float a = (float)i * (2.0f * 3.14159265358979f / 256.0f);
        const float v = 127.5f + 127.5f * std::cos(a);
        lut[i] = (uint8_t)v;
      }
      init = true;
    }
    return lut;
  }

  void free_pixels_() {
    if (pixels_)       { pal::psram_free(pixels_);       pixels_       = nullptr; }
    if (phase_offset_) { pal::psram_free(phase_offset_); phase_offset_ = nullptr; }
    if (base_color_)   { pal::psram_free(base_color_);   base_color_   = nullptr; }
    allocated_count_ = 0;
    plane_count_     = 0;
    moduleAllocBytes_ = 0;
  }

  void allocate_() {
    const uint32_t want_pixels = (uint32_t)width_ * (uint32_t)height_ * (uint32_t)depth_;
    const uint32_t want_plane  = (uint32_t)width_ * (uint32_t)height_;
    if (want_pixels == allocated_count_ && pixels_ && phase_offset_ && base_color_) {
      return;  // no-op on same-size rebuild
    }
    free_pixels_();
    if (want_pixels == 0) { ++revision_; ring_.allocate(0); return; }

    const size_t pixel_bytes = want_pixels * sizeof(RGB);
    const size_t phase_bytes = want_plane  * sizeof(uint16_t);
    const size_t color_bytes = want_plane  * sizeof(RGB);
    pixels_       = (RGB*)     pal::psram_alloc(pixel_bytes);
    phase_offset_ = (uint16_t*)pal::psram_alloc(phase_bytes);
    base_color_   = (RGB*)     pal::psram_alloc(color_bytes);
    if (!pixels_ || !phase_offset_ || !base_color_) {
      log("[ripples] alloc failed at %ux%ux%u (%u + %u + %u bytes)\n",
          (unsigned)width_, (unsigned)height_, (unsigned)depth_,
          (unsigned)pixel_bytes, (unsigned)phase_bytes, (unsigned)color_bytes);
      free_pixels_();
      ++revision_;
      ring_.allocate(0);
      return;
    }
    std::memset(pixels_, 0, pixel_bytes);
    allocated_count_  = want_pixels;
    plane_count_      = want_plane;
    moduleAllocBytes_ = pixel_bytes + phase_bytes + color_bytes;
    rebuild_phase_table_();
    rebuild_color_table_();
    if (!ring_.allocate(pixel_bytes)) {
      log("[ripples] ring alloc failed at %ux%ux%u — Art-Net out won't have a feed\n",
          (unsigned)width_, (unsigned)height_, (unsigned)depth_);
    }
    ++revision_;
    log("[ripples] allocated %ux%ux%u = %u bytes (+ tables %u B + ring 2x%u B)\n",
        (unsigned)width_, (unsigned)height_, (unsigned)depth_, (unsigned)pixel_bytes,
        (unsigned)(phase_bytes + color_bytes), (unsigned)pixel_bytes);
  }

  // Per-pixel `dist * 0.6` mapped to Q16 phase. Negative-to-uint16 cast is
  // safe here: dist is always non-negative; the float→int conversion below
  // happens on a positive value before it's narrowed.
  void rebuild_phase_table_() {
    if (!phase_offset_) return;
    const float cx = (float)width_  * 0.5f - 0.5f;
    const float cy = (float)height_ * 0.5f - 0.5f;
    constexpr float kPhaseScale = 65536.0f / (2.0f * 3.14159265358979f);  // rad → Q16
    const uint16_t w = (uint16_t)width_;
    const uint16_t h = (uint16_t)height_;
    for (uint16_t y = 0; y < h; ++y) {
      const float dy = (float)y - cy;
      const float dy2 = dy * dy;
      uint16_t* row = phase_offset_ + (uint32_t)y * w;
      for (uint16_t x = 0; x < w; ++x) {
        const float dx = (float)x - cx;
        const float dist = std::sqrt(dx * dx + dy2);
        // Wraps mod 65536 — safe even at 128×128 max dist ≈ 90.
        row[x] = (uint16_t)(uint32_t)(dist * 0.6f * kPhaseScale);
      }
    }
  }

  // Per-pixel full-bright HSV→RGB at hue = hue_base + dist·0.05.
  // Re-runs on hue_base change without touching the phase table.
  void rebuild_color_table_() {
    if (!base_color_) return;
    const float cx = (float)width_  * 0.5f - 0.5f;
    const float cy = (float)height_ * 0.5f - 0.5f;
    const uint16_t w = (uint16_t)width_;
    const uint16_t h = (uint16_t)height_;
    for (uint16_t y = 0; y < h; ++y) {
      const float dy = (float)y - cy;
      const float dy2 = dy * dy;
      RGB* row = base_color_ + (uint32_t)y * w;
      for (uint16_t x = 0; x < w; ++x) {
        const float dx = (float)x - cx;
        const float dist = std::sqrt(dx * dx + dy2);
        row[x] = RGB::fromHsv(hue_base_ + dist * 0.05f, 1.0f, 1.0f);
      }
    }
  }
};

}  // namespace pmm
