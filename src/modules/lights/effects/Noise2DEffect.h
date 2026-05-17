#pragma once
//
// Noise2DEffect — smooth animated value noise on the XY plane.
//
// Hash-based value noise with trilinear interpolation + smoothstep.
// No external libraries; works on PC and ESP32. Ported from v1 NoiseEffect2D.
//
// Controls:
//   layout: id of a layout module (text, default "layout-0")  [from base]
//   scale:  1–32 spatial frequency (default 4)
//   speed:  0–255 animation speed  (default 50)
//

#include <cmath>

#include "../../../pal/Pal.h"
#include "../RGB.h"
#include "PixelEffectBase.h"

namespace pmm {

class Noise2DEffect : public PixelEffectBase {
 public:
  const char* category() const override { return "effect"; }

 protected:
  void build_effect_controls() override {
    addControl(scale_, "scale", "slider", (uint8_t)1, (uint8_t)32);
    addControl(speed_, "speed", "slider", (uint8_t)0, (uint8_t)255);
  }

  void render_(RGB* px, uint16_t w, uint16_t h, uint16_t /*d*/) override {
    const float t      = (float)pal::micros() * 1e-6f * ((float)speed_ / 50.0f);
    const float scaleF = (float)scale_ * 0.1f;
    for (uint16_t y = 0; y < h; ++y)
      for (uint16_t x = 0; x < w; ++x) {
        uint8_t n = valueNoise_((float)x * scaleF, (float)y * scaleF, t);
        px[y * w + x] = RGB::fromHsv(n * (1.0f / 255.0f), 0.78f, 1.0f);
      }
  }

 private:
  uint8_t scale_ = 4;
  uint8_t speed_ = 50;

  static uint8_t hashXYT_(int32_t x, int32_t y, int32_t t) {
    uint32_t h = (uint32_t)(x * 1619 + y * 31337 + t * 6271);
    h ^= h >> 17; h *= 0xbf324b85u;
    h ^= h >> 13; h *= 0x9c7d5fadu;
    h ^= h >> 16;
    return (uint8_t)(h & 0xffu);
  }

  static float smoothstep_(float t) { return t * t * (3.0f - 2.0f * t); }

  static uint8_t valueNoise_(float x, float y, float t) {
    int32_t ix = (int32_t)std::floor(x), iy = (int32_t)std::floor(y), it = (int32_t)std::floor(t);
    float fx = smoothstep_(x - (float)ix);
    float fy = smoothstep_(y - (float)iy);
    float ft = smoothstep_(t - (float)it);
    float c000 = (float)hashXYT_(ix,   iy,   it);
    float c100 = (float)hashXYT_(ix+1, iy,   it);
    float c010 = (float)hashXYT_(ix,   iy+1, it);
    float c110 = (float)hashXYT_(ix+1, iy+1, it);
    float c001 = (float)hashXYT_(ix,   iy,   it+1);
    float c101 = (float)hashXYT_(ix+1, iy,   it+1);
    float c011 = (float)hashXYT_(ix,   iy+1, it+1);
    float c111 = (float)hashXYT_(ix+1, iy+1, it+1);
    float x0 = c000 + fx * (c100 - c000);
    float x1 = c010 + fx * (c110 - c010);
    float x2 = c001 + fx * (c101 - c001);
    float x3 = c011 + fx * (c111 - c011);
    float y0 = x0 + fy * (x1 - x0);
    float y1 = x2 + fy * (x3 - x2);
    return (uint8_t)(y0 + ft * (y1 - y0));
  }
};

}  // namespace pmm
