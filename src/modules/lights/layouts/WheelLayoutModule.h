#pragma once
//
// WheelLayoutModule — spokes × ledsPerSpoke LEDs as radial lines from centre.
//
// Spoke s has ledsPerSpoke LEDs at distances 1..ledsPerSpoke from the centre,
// along angle s * 2π / spokes. The virtual bounding grid is
// (2*ledsPerSpoke+1) × (2*ledsPerSpoke+1) × 1.
// Physical index order: spoke 0 inner→outer, then spoke 1, etc.
//
// Provides the same geometry API as GridLayoutModule.
//
// Controls:
//   spokes:       2–64  (default 8)
//   ledsPerSpoke: 1–128 (default 15)
//

#include <cmath>
#include <cstring>

#include "LayoutModule.h"

namespace pmm {

class WheelLayoutModule : public LayoutModule {
 public:
  void onBuildControls() override {
    addControl(spokes_,         "spokes",       "slider", (uint16_t)2,  (uint16_t)64);
    addControl(leds_per_spoke_, "ledsPerSpoke", "slider", (uint16_t)1,  (uint16_t)128);
  }

  uint16_t width()  const override { return dim_(); }
  uint16_t height() const override { return dim_(); }
  uint16_t depth()  const override { return 1; }
  uint32_t physical_count() const override { return (uint32_t)spokes_ * leds_per_spoke_; }

  // Map physical LED → logical pixel index in the (w×h) grid.
  // Physical index = spoke_index * ledsPerSpoke + led_within_spoke (0=inner).
  uint32_t map(uint32_t phys) const override {
    if (phys >= physical_count()) return 0;
    const uint16_t d = dim_();
    const uint32_t s = phys / leds_per_spoke_;
    const uint32_t p = phys % leds_per_spoke_ + 1;  // distance 1..ledsPerSpoke
    const float cx    = (float)leds_per_spoke_;
    const float cy    = (float)leds_per_spoke_;
    const double angle = (double)s * 2.0 * 3.14159265358979 / (double)spokes_;
    const int32_t xi  = clamp_((int32_t)std::lround(cx + (float)p * (float)std::cos(angle)), d);
    const int32_t yi  = clamp_((int32_t)std::lround(cy + (float)p * (float)std::sin(angle)), d);
    return (uint32_t)yi * d + (uint32_t)xi;
  }

 private:
  uint16_t spokes_         = 8;
  uint16_t leds_per_spoke_ = 15;

  uint16_t dim_() const { return (uint16_t)(2u * leds_per_spoke_ + 1u); }

  static int32_t clamp_(int32_t v, uint16_t dim) {
    if (v < 0) return 0;
    if (v >= (int32_t)dim) return (int32_t)(dim - 1);
    return v;
  }
};

}  // namespace pmm
