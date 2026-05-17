#pragma once
//
// SineEffect — 3D waveform (one colour per axis) or radial ripples.
//
// Type 0 (sine): per-pixel RGB from sin(freq*axis + tick), 2π/3 phase per
//   channel. Type 1 (ripples): radial sine waves from centre, HSV wheel.
// Ported from v1 SineEffectModule. The control set switches on `type`.
//
// Controls:
//   layout:    id of a layout module (text, default "layout-0")  [from base]
//   type:      sine | ripples (default sine)
//   sine:      frequency 1–20, amplitude 0–255, waveform sine|sq|tri|saw
//   ripples:   speed 0–99, interval 1–254
//

#include <cmath>
#include <cstring>

#include "../../../pal/Pal.h"
#include "../RGB.h"
#include "PixelEffectBase.h"

namespace pmm {

class SineEffect : public PixelEffectBase {
 public:
  const char* category() const override { return "effect"; }

 protected:
  bool is3d() const override { return true; }

  void build_effect_controls() override {
    addControl(type_, "type", kTypes, kTypeCount);
    if (type_ == 0) {
      addControl(frequency_, "frequency", "slider", (uint8_t)1, (uint8_t)20);
      addControl(amplitude_, "amplitude", "slider", (uint8_t)0, (uint8_t)255);
      addControl(waveform_,  "waveform",  kWaveforms, kWaveformCount);
    } else {
      addControl(speed_,    "speed",    "slider", (uint8_t)0, (uint8_t)99);
      addControl(interval_, "interval", "slider", (uint8_t)1, (uint8_t)254);
    }
  }

  void on_control_(const char* key) override {
    if (std::strcmp(key, "type") == 0) rebuild_controls_();
  }

  void render_(RGB* px, uint16_t w, uint16_t h, uint16_t d) override {
    if (type_ == 1) render_ripples_(px, w, h, d);
    else            render_sine_(px, w, h, d);
  }

 private:
  static constexpr const char* kTypes[]       = {"sine", "ripples"};
  static constexpr uint8_t     kTypeCount     = 2;
  static constexpr const char* kWaveforms[]   = {"sine", "square", "triangle", "sawtooth"};
  static constexpr uint8_t     kWaveformCount = 4;
  static constexpr float       kPi = 3.14159265f;

  uint8_t  type_      = 0;
  uint8_t  frequency_ = 1;
  uint8_t  amplitude_ = 255;
  uint8_t  waveform_  = 0;
  uint8_t  speed_     = 50;
  uint8_t  interval_  = 128;
  uint32_t tick_      = 0;

  float wave_(float p) const {
    switch (waveform_) {
      case 1: return std::sin(p) >= 0.0f ? 1.0f : -1.0f;
      case 2: return std::asin(std::sin(p)) * (2.0f / kPi);
      case 3: return std::fmod(p, 2.0f * kPi) / kPi - 1.0f;
      default: return std::sin(p);
    }
  }

  void render_sine_(RGB* px, uint16_t w, uint16_t h, uint16_t d) {
    const float ampF = (float)amplitude_ / 255.0f;
    const float fx   = (float)frequency_;
    const uint32_t plane = (uint32_t)w * h;
    for (uint16_t z = 0; z < d; ++z)
      for (uint16_t y = 0; y < h; ++y)
        for (uint16_t x = 0; x < w; ++x)
          px[(uint32_t)z * plane + y * w + x] = {
            (uint8_t)(ampF * (wave_(fx * (x + tick_))          + 1.0f) * 127.5f),
            (uint8_t)(ampF * (wave_(fx * (y + tick_) + 2.094f) + 1.0f) * 127.5f),
            (uint8_t)(ampF * (wave_(fx * (z + tick_) + 4.189f) + 1.0f) * 127.5f),
          };
    ++tick_;
  }

  void render_ripples_(RGB* px, uint16_t w, uint16_t h, uint16_t d) {
    const uint32_t plane = (uint32_t)w * h;
    std::memset(px, 0, plane * d * sizeof(RGB));
    const float ri = 1.3f * ((255.0f - (float)interval_) / 128.0f) * std::sqrt((float)h);
    if (ri < 0.01f) return;
    const uint32_t frameUs = pal::micros();
    const float ti = (float)frameUs * 1e-3f / (100.0f - (float)speed_) / 6.4f;
    const float cx = (float)(w - 1) * 0.5f;
    const float cz = (float)(d - 1) * 0.5f;
    for (uint16_t z = 0; z < d; ++z)
      for (uint16_t x = 0; x < w; ++x) {
        const float dx   = (float)x - cx;
        const float dz   = (float)z - cz;
        const float dist = std::sqrt(dx*dx + dz*dz) / 9.899495f * (float)h;
        const float phase = dist / ri + ti;
        const uint16_t y = (uint16_t)std::floor((float)h * 0.5f * (1.0f + std::sin(phase)));
        if (y < h) {
          const uint8_t hue = (uint8_t)(frameUs / 50000u + x * 3u + z * 7u);
          px[(uint32_t)z * plane + y * w + x] = RGB::fromHsv(hue * (1.0f/255.0f), 1.0f, 1.0f);
        }
      }
  }
};

}  // namespace pmm
