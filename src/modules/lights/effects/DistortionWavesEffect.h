#pragma once
//
// DistortionWavesEffect — two interfering sine waves mapped to hue.
//
// val = sin(x*freq_x*2π/W + t) + sin(y*freq_y*2π/H + t*1.3); val∈[-2,2]→hue.
// Ported from WLED / v1 DistortionWaves2DEffect.
//
// Controls:
//   layout:  id of a layout module (text, default "layout-0")  [from base]
//   freq_x:  1–8 (default 3)
//   freq_y:  1–8 (default 3)
//   speed:   0–100 (default 50)
//

#include <cmath>

#include "../../../pal/Pal.h"
#include "../RGB.h"
#include "PixelEffectBase.h"

namespace pmm {

class DistortionWavesEffect : public PixelEffectBase {
 public:
  const char* category() const override { return "effect"; }

 protected:
  void build_effect_controls() override {
    addControl(freq_x_, "freq_x", "slider", (uint8_t)1, (uint8_t)8);
    addControl(freq_y_, "freq_y", "slider", (uint8_t)1, (uint8_t)8);
    addControl(speed_,  "speed",  "slider", (uint8_t)0, (uint8_t)100);
  }

  void render_(RGB* px, uint16_t w, uint16_t h, uint16_t /*d*/) override {
    const float t  = (float)pal::micros() * 1e-6f * ((float)speed_ / 20.0f);
    const float tw = (float)freq_x_ * 6.283185f / (float)w;
    const float th = (float)freq_y_ * 6.283185f / (float)h;
    for (uint16_t y = 0; y < h; ++y)
      for (uint16_t x = 0; x < w; ++x) {
        float val = std::sin((float)x * tw + t) + std::sin((float)y * th + t * 1.3f);
        uint8_t hue = (uint8_t)((val + 2.0f) * 63.75f);
        px[y * w + x] = RGB::fromHsv(hue * (1.0f / 255.0f), 0.94f, 1.0f);
      }
  }

 private:
  uint8_t freq_x_ = 3;
  uint8_t freq_y_ = 3;
  uint8_t speed_  = 50;
};

}  // namespace pmm
