#pragma once
//
// LinesEffect — three coloured planes (Red/YZ, Green/XZ, Blue/XY) sweeping
// through a 3D volume in sync at a given BPM. Works on 1D/2D/3D panels.
// Ported from v1 LinesEffectModule.
//
// Controls:
//   layout: id of a layout module (text, default "layout-0")  [from base]
//   bpm:    1–240 (default 30)
//   axis:   all | x | y | z (default all)
//

#include <cstring>

#include "../../../pal/Pal.h"
#include "../RGB.h"
#include "PixelEffectBase.h"

namespace pmm {

class LinesEffect : public PixelEffectBase {
 public:
  const char* category() const override { return "effect"; }

 protected:
  bool is3d() const override { return true; }

  void build_effect_controls() override {
    addControl(bpm_,  "bpm",  "slider", (uint8_t)1, (uint8_t)240);
    addControl(axis_, "axis", kAxes, kAxisCount);
  }

  void render_(RGB* px, uint16_t w, uint16_t h, uint16_t d) override {
    const uint32_t plane = (uint32_t)w * h;
    std::memset(px, 0, plane * d * sizeof(RGB));
    const uint16_t beat = beat16_();

    if (w > 1 && (axis_ == 0 || axis_ == 1)) {
      const uint16_t x = (uint16_t)((uint32_t)beat * (w - 1) / 65535u);
      for (uint16_t z = 0; z < d; ++z)
        for (uint16_t y = 0; y < h; ++y)
          px[(uint32_t)z * plane + y * w + x] = {255, 0, 0};
    }
    if (h > 1 && (axis_ == 0 || axis_ == 2)) {
      const uint16_t y = (uint16_t)((uint32_t)beat * (h - 1) / 65535u);
      for (uint16_t z = 0; z < d; ++z)
        for (uint16_t x = 0; x < w; ++x)
          px[(uint32_t)z * plane + y * w + x] = {0, 255, 0};
    }
    if (d > 1 && (axis_ == 0 || axis_ == 3)) {
      const uint16_t z = (uint16_t)((uint32_t)beat * (d - 1) / 65535u);
      for (uint16_t y = 0; y < h; ++y)
        for (uint16_t x = 0; x < w; ++x)
          px[(uint32_t)z * plane + y * w + x] = {0, 0, 255};
    }
  }

 private:
  static constexpr const char* kAxes[]    = {"all", "x", "y", "z"};
  static constexpr uint8_t     kAxisCount = 4;

  uint8_t bpm_  = 30;
  uint8_t axis_ = 0;

  // Sawtooth 0–65535 at bpm_ beats per minute.
  uint16_t beat16_() const {
    const uint32_t ms     = pal::millis();
    const uint32_t period = 60000u / (uint32_t)bpm_;
    return (uint16_t)((ms % period) * 65535u / period);
  }
};

}  // namespace pmm
