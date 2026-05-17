#pragma once
//
// RipplesEffect — radial sine ripples on a 2D panel (with optional repeated
// depth slices). Ported onto PixelEffectBase: the layout-resolve / pixels_ /
// owned DataBuffer<RGB> / ADR 0005 teardown / resize-poll spine lives in the
// base; this file is the ripple-specific tables + inner loop only.
//
// Geometry: from a linked layout module (Grid/Ring/Wheel), resolved by the
// `layout` input or a parent. Fallback 16×16×1. No geometry controls here.
//
// Controls:
//   layout:   id of a layout module (text, default "layout-0")  [from base]
//   speed:    0–255 → 0.0–10.0 rad/s (default 26 ≈ 1.0 rad/s)
//   hue_base: 0–255 → 0.0–1.0 hue (default 153 ≈ 0.6)
//
// Extra per-pixel tables (allocated in on_geometry_, w*h each):
//   phase_offset_ — Q16 radial phase, uint16
//   base_color_   — precomputed hue-ramped colour, RGB
//
// Hot-path: inner loop is one Q16 subtract, one LUT load, three uint8
// multiply-shifts per pixel. No sqrt/cos/HSV per frame.
//

#include <cmath>
#include <cstdint>
#include <cstring>

#include "../../pal/Pal.h"
#include "../../pal/PalHeap.h"
#include "RGB.h"
#include "effects/PixelEffectBase.h"

namespace pmm {

class RipplesEffect : public PixelEffectBase {
 public:
  const char* category() const override { return "effect"; }

  void teardown() override {
    free_tables_();
    PixelEffectBase::teardown();
  }

 protected:
  bool is3d() const override { return true; }

  void build_effect_controls() override {
    addControl(speed_,    "speed",    "slider", (uint8_t)0, (uint8_t)255);
    addControl(hue_base_, "hue_base", "slider", (uint8_t)0, (uint8_t)255);
  }

  void on_control_(const char* key) override {
    if (std::strcmp(key, "hue_base") == 0) rebuild_color_table_();
  }

  // Base calls this after (re)allocating pixels_ at the new geometry.
  void on_geometry_(uint16_t w, uint16_t h, uint16_t /*d*/) override {
    free_tables_();
    tw_ = w; th_ = h;
    const uint32_t plane = (uint32_t)w * h;
    phase_offset_ = (uint16_t*)pal::psram_alloc(plane * sizeof(uint16_t));
    base_color_   = (RGB*)     pal::psram_alloc(plane * sizeof(RGB));
    rebuild_phase_table_();
    rebuild_color_table_();
  }

  void render_(RGB* px, uint16_t w, uint16_t h, uint16_t d) override {
    if (!phase_offset_ || !base_color_) return;
    const uint8_t* const bri_lut = cos_bri_lut_();
    const uint32_t plane = (uint32_t)w * h;

    const float arg = (float)pal::millis() * 0.001f * (speed_ * (10.0f / 255.0f)) * 2.0f;
    const float wrapped = std::fmod(arg, 2.0f * 3.14159265358979f);
    const uint16_t t_q = (uint16_t)(int32_t)(wrapped * (65536.0f / (2.0f * 3.14159265358979f)));

    for (uint16_t z = 0; z < d; ++z) {
      RGB* out = px + (uint32_t)z * plane;
      for (uint32_t i = 0; i < plane; ++i) {
        const uint16_t phase = (uint16_t)(phase_offset_[i] - t_q);
        const uint8_t  bri   = bri_lut[phase >> 8];
        const RGB      c     = base_color_[i];
        out[i].r = (uint8_t)((uint16_t)c.r * bri >> 8);
        out[i].g = (uint8_t)((uint16_t)c.g * bri >> 8);
        out[i].b = (uint8_t)((uint16_t)c.b * bri >> 8);
      }
    }
  }

 private:
  uint8_t  speed_    = 26;
  uint8_t  hue_base_ = 153;

  uint16_t  tw_ = 0, th_ = 0;            // geometry the tables were built for
  uint16_t* phase_offset_ = nullptr;
  RGB*      base_color_   = nullptr;

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

  void free_tables_() {
    if (phase_offset_) { pal::psram_free(phase_offset_); phase_offset_ = nullptr; }
    if (base_color_)   { pal::psram_free(base_color_);   base_color_   = nullptr; }
    tw_ = th_ = 0;
  }

  void rebuild_phase_table_() {
    if (!phase_offset_) return;
    const float cx = (float)tw_ * 0.5f - 0.5f;
    const float cy = (float)th_ * 0.5f - 0.5f;
    constexpr float kPhaseScale = 65536.0f / (2.0f * 3.14159265358979f);
    for (uint32_t y = 0; y < th_; ++y) {
      const float dy = (float)y - cy;
      const float dy2 = dy * dy;
      uint16_t* row = phase_offset_ + y * tw_;
      for (uint32_t x = 0; x < tw_; ++x) {
        const float dx = (float)x - cx;
        row[x] = (uint16_t)(uint32_t)(std::sqrt(dx * dx + dy2) * 0.6f * kPhaseScale);
      }
    }
  }

  void rebuild_color_table_() {
    if (!base_color_) return;
    const float cx = (float)tw_ * 0.5f - 0.5f;
    const float cy = (float)th_ * 0.5f - 0.5f;
    for (uint32_t y = 0; y < th_; ++y) {
      const float dy = (float)y - cy;
      const float dy2 = dy * dy;
      RGB* row = base_color_ + y * tw_;
      for (uint32_t x = 0; x < tw_; ++x) {
        const float dx = (float)x - cx;
        row[x] = RGB::fromHsv(hue_base_ * (1.0f / 255.0f) + std::sqrt(dx * dx + dy2) * 0.05f, 1.0f, 1.0f);
      }
    }
  }
};

}  // namespace pmm
