#pragma once
//
// RipplesEffect — radial sine ripples on a 2D panel (with optional repeated
// depth slices). Writes a working RGB buffer each tick, then publishes it
// into a DataBuffer<RGB> owned here and declared in DataRegistry so
// PreviewModule and ArtnetOutModule can read it zero-copy from the same slot.
//
// Controls (all 0–255 for DMX compatibility):
//   width, height: 1..128 (default 16) — buffer geometry
//   depth: 1..16 (default 1) — repeats the 2D pattern per slice
//   speed: 0–255 → 0.0–10.0 rad/s (default 26 ≈ 1.0 rad/s)
//   hue_base: 0–255 → 0.0–1.0 hue (default 153 ≈ 0.6)
//
// Allocation (onAllocateMemory):
//   pixels_       — working copy, pal::psram_alloc, w*h*d * 3B
//   phase_offset_ — per-pixel Q16 phase table, w*h * 2B
//   base_color_   — per-pixel precomputed colour, w*h * 3B
//   buf_          — DataBuffer<RGB>, one slot, w*h*d * 3B (consumers read from here)
//
// Hot-path: inner loop is one Q16 subtract, one LUT load, three uint8
// multiply-shifts per pixel. No sqrt/cos/HSV per frame.
//

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../../core/DataBuffer.h"
#include "../../core/DataRegistry.h"
#include "../../core/MoonModule.h"
#include "../../pal/Pal.h"
#include "../../pal/PalHeap.h"
#include "../system/Logger.h"
#include "Pixelable.h"
#include "RGB.h"

namespace pmm {

class RipplesEffect : public MoonModule, public PixelSource {
 public:
  const char* category() const override { return "effect"; }

  void setup() override {}

  void onBuildControls() override {
    addControl(width_,    "width",    "slider", (uint8_t)1, (uint8_t)128);
    addControl(height_,   "height",   "slider", (uint8_t)1, (uint8_t)128);
    addControl(depth_,    "depth",    "slider", (uint8_t)1, (uint8_t)16);
    addControl(speed_,    "speed",    "slider", (uint8_t)0, (uint8_t)255);
    addControl(hue_base_, "hue_base", "slider", (uint8_t)0, (uint8_t)255);
  }

  void onAllocateMemory() override {
    allocate_();
  }

  void onUpdate(const char* key) override {
    if (std::strcmp(key, "width")  == 0 ||
        std::strcmp(key, "height") == 0 ||
        std::strcmp(key, "depth")  == 0) {
      allocate_();
    } else if (std::strcmp(key, "hue_base") == 0) {
      rebuild_color_table_();
    }
  }

  void loop20ms() override {
    if (!pixels_ || !phase_offset_ || !base_color_) return;
    const uint8_t* const bri_lut = cos_bri_lut_();
    const uint16_t w = (uint16_t)width_;
    const uint16_t h = (uint16_t)height_;
    const uint16_t d = (uint16_t)depth_;
    const uint32_t plane = (uint32_t)w * h;

    const float arg = (float)pal::millis() * 0.001f * (speed_ * (10.0f / 255.0f)) * 2.0f;
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

    // Publish into the shared DataBuffer — consumers (Preview, ArtNet) read
    // zero-copy from the same single slot.
    if (buf_ && buf_->valid()) {
      RGB* dst = buf_->acquire_write();
      std::memcpy(dst, pixels_, allocated_count_ * sizeof(RGB));
      buf_->publish();
      revision_++;
    }
  }

  void teardown() override {
    if (buf_) {
      DataRegistry::instance().undeclare(buf_);
      delete buf_;
      buf_ = nullptr;
    }
    free_pixels_();
  }

  PixelBufferRef pixelBuffer() const override {
    return { pixels_,
             (uint16_t)width_,
             (uint16_t)height_,
             (uint16_t)depth_,
             revision_ };
  }

 private:
  uint8_t width_    = 16;
  uint8_t height_   = 16;
  uint8_t depth_    = 1;
  uint8_t speed_    = 26;
  uint8_t hue_base_ = 153;

  RGB*              pixels_          = nullptr;
  uint32_t          revision_        = 0;
  uint32_t          allocated_count_ = 0;
  DataBuffer<RGB>*  buf_             = nullptr;  // owned here, declared in DataRegistry

  uint16_t* phase_offset_ = nullptr;
  RGB*      base_color_   = nullptr;
  uint32_t  plane_count_  = 0;

  static const uint8_t* cos_bri_lut_() {
    static uint8_t lut[256];
    static bool    init = false;
    if (!init) {
      for (int i = 0; i < 256; ++i) {
        const float a = (float)i * (2.0f * 3.14159265358979f / 256.0f);
        lut[i] = (uint8_t)(127.5f + 127.5f * std::cos(a));
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
    if (want_pixels == allocated_count_ && pixels_ && phase_offset_ && base_color_) return;

    free_pixels_();

    if (buf_) {
      DataRegistry::instance().undeclare(buf_);
      delete buf_;
      buf_ = nullptr;
    }

    if (want_pixels == 0) { ++revision_; return; }

    const size_t pixel_bytes = want_pixels * sizeof(RGB);
    const size_t phase_bytes = want_plane  * sizeof(uint16_t);
    const size_t color_bytes = want_plane  * sizeof(RGB);
    pixels_       = (RGB*)     pal::psram_alloc(pixel_bytes);
    phase_offset_ = (uint16_t*)pal::psram_alloc(phase_bytes);
    base_color_   = (RGB*)     pal::psram_alloc(color_bytes);
    if (!pixels_ || !phase_offset_ || !base_color_) {
      log("[ripples] alloc failed at %ux%ux%u\n",
          (unsigned)width_, (unsigned)height_, (unsigned)depth_);
      free_pixels_();
      ++revision_;
      return;
    }
    std::memset(pixels_, 0, pixel_bytes);
    allocated_count_  = want_pixels;
    plane_count_      = want_plane;
    moduleAllocBytes_ = pixel_bytes + phase_bytes + color_bytes;
    rebuild_phase_table_();
    rebuild_color_table_();

    // Declare a DataBuffer<RGB> in the registry. Buffer owned by this module;
    // registry exposes it to consumers (Preview, ArtNet) by id.
    buf_ = new DataBuffer<RGB>();
    if (!buf_->allocate(want_pixels)) {
      log("[ripples] buf alloc failed at %ux%ux%u\n",
          (unsigned)width_, (unsigned)height_, (unsigned)depth_);
      delete buf_;
      buf_ = nullptr;
    } else {
      DataRegistry::instance().declare(id(), buf_, want_pixels, sizeof(RGB),
                                       width_, height_, depth_);
      log("[ripples] allocated %ux%ux%u pixels=%uB tables=%uB\n",
          (unsigned)width_, (unsigned)height_, (unsigned)depth_,
          (unsigned)pixel_bytes, (unsigned)(phase_bytes + color_bytes));
    }
    ++revision_;
  }

  void rebuild_phase_table_() {
    if (!phase_offset_) return;
    const float cx = (float)width_  * 0.5f - 0.5f;
    const float cy = (float)height_ * 0.5f - 0.5f;
    constexpr float kPhaseScale = 65536.0f / (2.0f * 3.14159265358979f);
    const uint16_t w = (uint16_t)width_;
    const uint16_t h = (uint16_t)height_;
    for (uint16_t y = 0; y < h; ++y) {
      const float dy = (float)y - cy;
      const float dy2 = dy * dy;
      uint16_t* row = phase_offset_ + (uint32_t)y * w;
      for (uint16_t x = 0; x < w; ++x) {
        const float dx = (float)x - cx;
        row[x] = (uint16_t)(uint32_t)(std::sqrt(dx * dx + dy2) * 0.6f * kPhaseScale);
      }
    }
  }

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
        row[x] = RGB::fromHsv(hue_base_ * (1.0f / 255.0f) + std::sqrt(dx * dx + dy2) * 0.05f, 1.0f, 1.0f);
      }
    }
  }
};

}  // namespace pmm
