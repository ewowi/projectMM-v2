#pragma once
//
// RipplesEffect — radial sine ripples on a 2D panel (with optional repeated
// depth slices). Writes a working RGB buffer each tick, then publishes it
// into a DataBuffer<RGB> owned here and declared in DataRegistry so
// PreviewModule and ArtnetOutModule can read it zero-copy from the same slot.
//
// Geometry: comes from a linked GridLayoutModule (resolved by the `layout` control,
// a text field holding the layout module's id). Fallback when no layout is
// linked: 16×16×1. The effect carries no geometry controls of its own.
//
// Controls:
//   layout:   id of a GridLayoutModule (text, default "layout-0")
//   speed:    0–255 → 0.0–10.0 rad/s (default 26 ≈ 1.0 rad/s)
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
#include "../../core/ModuleManager.h"
#include "../../pal/Pal.h"
#include "../../pal/PalHeap.h"
#include "../system/Logger.h"
#include "GridLayoutModule.h"
#include "Pixelable.h"
#include "RGB.h"

namespace pmm {

class RipplesEffect : public MoonModule, public PixelSource {
 public:
  const char* category() const override { return "effect"; }

  void setup() override {
    resolve_layout_();
  }

  void onBuildControls() override {
    addControl(layout_id_, sizeof(layout_id_), "layout",   "text");
    addControl(speed_,                         "speed",    "slider", (uint8_t)0, (uint8_t)255);
    addControl(hue_base_,                      "hue_base", "slider", (uint8_t)0, (uint8_t)255);
  }

  void onAllocateMemory() override {
    resolve_layout_();
    allocate_();
  }

  void onUpdate(const char* key) override {
    if (std::strcmp(key, "layout") == 0) {
      resolve_layout_();
      allocate_();
    } else if (std::strcmp(key, "hue_base") == 0) {
      rebuild_color_table_();
    }
  }

  void loop1s() override {
    if (!layout_) resolve_layout_();
    if (layout_ && (layout_->width() != w_ || layout_->height() != h_ || layout_->depth() != d_))
      allocate_();
  }

  void loop20ms() override {
    // Re-resolve if layout was added after this effect.
    if (!layout_) resolve_layout_();

    if (!pixels_ || !phase_offset_ || !base_color_) return;
    const uint8_t* const bri_lut = cos_bri_lut_();
    const uint32_t w = w_;
    const uint32_t h = h_;
    const uint32_t d = d_;
    const uint32_t plane = w * h;

    const float arg = (float)pal::millis() * 0.001f * (speed_ * (10.0f / 255.0f)) * 2.0f;
    const float wrapped = std::fmod(arg, 2.0f * 3.14159265358979f);
    const uint16_t t_q = (uint16_t)(int32_t)(wrapped * (65536.0f / (2.0f * 3.14159265358979f)));

    for (uint32_t z = 0; z < d; ++z) {
      RGB* out = pixels_ + z * plane;
      for (uint32_t i = 0; i < plane; ++i) {
        const uint16_t phase = (uint16_t)(phase_offset_[i] - t_q);
        const uint8_t  bri   = bri_lut[phase >> 8];
        const RGB      c     = base_color_[i];
        out[i].r = (uint8_t)((uint16_t)c.r * bri >> 8);
        out[i].g = (uint8_t)((uint16_t)c.g * bri >> 8);
        out[i].b = (uint8_t)((uint16_t)c.b * bri >> 8);
      }
    }

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
    layout_ = nullptr;
  }

  PixelBufferRef pixelBuffer() const override {
    return { pixels_,
             (uint16_t)w_,
             (uint16_t)h_,
             (uint16_t)d_,
             revision_ };
  }

 private:
  char     layout_id_[24] = "layout-0";
  uint8_t  speed_         = 26;
  uint8_t  hue_base_      = 153;

  // Resolved geometry — set from GridLayoutModule or fallback defaults.
  uint32_t w_ = 16;
  uint32_t h_ = 16;
  uint32_t d_ = 1;

  GridLayoutModule*    layout_          = nullptr;
  RGB*             pixels_          = nullptr;
  uint32_t         revision_        = 0;
  uint32_t         allocated_count_ = 0;
  DataBuffer<RGB>* buf_             = nullptr;

  uint16_t* phase_offset_ = nullptr;
  RGB*      base_color_   = nullptr;
  uint32_t  plane_count_  = 0;

  // Sprint 17 resolution precedence (domain-local, cold path only):
  //   1. parent that resolves geometry (a parent layer's layout, or a
  //      parent that IS a GridLayoutModule) — the structural source wins
  //   2. own "layout" input (layout_id_) — explicit override / lone effect
  //   3. 16×16 default (handled by geometry_from_layout_ when layout_==null)
  // The `layer` rung is automatic once a parent-flagged "layer" input lands
  // (deferred EffectGroup): walking parent() already covers it — no new code.
  void resolve_layout_() {
    layout_ = nullptr;
    if (!manager_) return;
    for (MoonModule* p = parent(); p; p = p->parent()) {
      if (std::strcmp(p->type(), "layout") == 0) {
        layout_ = static_cast<GridLayoutModule*>(p);
        return;
      }
    }
    MoonModule* m = manager_->find(layout_id_);
    if (m && std::strcmp(m->type(), "layout") == 0)
      layout_ = static_cast<GridLayoutModule*>(m);
  }

  void geometry_from_layout_() {
    if (layout_) {
      w_ = layout_->width();
      h_ = layout_->height();
      d_ = layout_->depth();
    } else {
      w_ = 16; h_ = 16; d_ = 1;
    }
  }

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
    geometry_from_layout_();
    const uint32_t want_pixels = w_ * h_ * d_;
    const uint32_t want_plane  = w_ * h_;
    if (want_pixels == allocated_count_ && pixels_ && phase_offset_ && base_color_) return;

    free_pixels_();

    if (want_pixels == 0) { ++revision_; return; }

    const size_t pixel_bytes = want_pixels * sizeof(RGB);
    const size_t phase_bytes = want_plane  * sizeof(uint16_t);
    const size_t color_bytes = want_plane  * sizeof(RGB);
    pixels_       = (RGB*)     pal::psram_alloc(pixel_bytes);
    phase_offset_ = (uint16_t*)pal::psram_alloc(phase_bytes);
    base_color_   = (RGB*)     pal::psram_alloc(color_bytes);
    if (!pixels_ || !phase_offset_ || !base_color_) {
      log("[ripples] alloc failed at %ux%ux%u\n",
          (unsigned)w_, (unsigned)h_, (unsigned)d_);
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

    // Keep buf_ alive across geometry changes — deleting it while a reader holds
    // the raw slot pointer (from try_acquire_read) causes a use-after-free on
    // the second core. Instead resize in place: allocate() resets published_ to
    // kNone so readers get nullptr until the next publish (safe skip).
    if (!buf_) buf_ = new DataBuffer<RGB>();
    if (!buf_->allocate(want_pixels)) {
      log("[ripples] buf alloc failed at %ux%ux%u\n",
          (unsigned)w_, (unsigned)h_, (unsigned)d_);
    } else {
      // declare() updates the existing entry if already registered.
      DataRegistry::instance().declare(id(), buf_, want_pixels, sizeof(RGB),
                                       (uint16_t)w_, (uint16_t)h_, (uint16_t)d_);
      log("[ripples] allocated %ux%ux%u pixels=%uB tables=%uB\n",
          (unsigned)w_, (unsigned)h_, (unsigned)d_,
          (unsigned)pixel_bytes, (unsigned)(phase_bytes + color_bytes));
    }
    ++revision_;
  }

  void rebuild_phase_table_() {
    if (!phase_offset_) return;
    const float cx = (float)w_ * 0.5f - 0.5f;
    const float cy = (float)h_ * 0.5f - 0.5f;
    constexpr float kPhaseScale = 65536.0f / (2.0f * 3.14159265358979f);
    for (uint32_t y = 0; y < h_; ++y) {
      const float dy = (float)y - cy;
      const float dy2 = dy * dy;
      uint16_t* row = phase_offset_ + y * w_;
      for (uint32_t x = 0; x < w_; ++x) {
        const float dx = (float)x - cx;
        row[x] = (uint16_t)(uint32_t)(std::sqrt(dx * dx + dy2) * 0.6f * kPhaseScale);
      }
    }
  }

  void rebuild_color_table_() {
    if (!base_color_) return;
    const float cx = (float)w_ * 0.5f - 0.5f;
    const float cy = (float)h_ * 0.5f - 0.5f;
    for (uint32_t y = 0; y < h_; ++y) {
      const float dy = (float)y - cy;
      const float dy2 = dy * dy;
      RGB* row = base_color_ + y * w_;
      for (uint32_t x = 0; x < w_; ++x) {
        const float dx = (float)x - cx;
        row[x] = RGB::fromHsv(hue_base_ * (1.0f / 255.0f) + std::sqrt(dx * dx + dy2) * 0.05f, 1.0f, 1.0f);
      }
    }
  }
};

}  // namespace pmm
