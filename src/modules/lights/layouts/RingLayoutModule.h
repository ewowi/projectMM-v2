#pragma once
//
// RingLayoutModule — N LEDs arranged in a circle.
//
// Maps physical LED i to the nearest cell on a (2*radius+1)^2 virtual grid:
//   (cx + radius * cos(i * 2π / ledCount), cy + radius * sin(i * 2π / ledCount))
//
// Provides the same geometry API as GridLayoutModule so effects can link to
// either layout type via the "layout" control. The virtual bounding box is
// (2*radius+1) × (2*radius+1) × 1.
//
// Controls:
//   ledCount: 3–512 (default 60)
//   radius:   1–255 (default 30)
//

#include <algorithm>
#include <cmath>
#include <cstring>

#include "LayoutModule.h"

namespace pmm {

class RingLayoutModule : public LayoutModule {
 public:
  void onBuildControls() override {
    addControl(led_count_, "ledCount", "slider", (uint16_t)3,  (uint16_t)512);
    addControl(radius_,    "radius",   "slider", (uint16_t)1,  (uint16_t)255);
  }

  uint16_t width()  const override { return dim_(); }
  uint16_t height() const override { return dim_(); }
  uint16_t depth()  const override { return 1; }
  uint32_t physical_count() const override { return led_count_; }

  // Map physical LED index → logical pixel index in the (w×h) grid.
  uint32_t map(uint32_t phys) const override {
    if (phys >= led_count_) return 0;
    const uint16_t d  = dim_();
    const float cx    = (float)radius_;
    const float cy    = (float)radius_;
    const double angle = (double)phys * 2.0 * 3.14159265358979 / (double)led_count_;
    const int32_t xi  = clamp_((int32_t)std::lround(cx + (float)radius_ * (float)std::cos(angle)), d);
    const int32_t yi  = clamp_((int32_t)std::lround(cy + (float)radius_ * (float)std::sin(angle)), d);
    return (uint32_t)yi * d + (uint32_t)xi;
  }

 private:
  uint16_t led_count_ = 60;
  uint16_t radius_    = 30;

  uint16_t dim_() const { return (uint16_t)(2u * radius_ + 1u); }

  static int32_t clamp_(int32_t v, uint16_t dim) {
    if (v < 0) return 0;
    if (v >= (int32_t)dim) return (int32_t)(dim - 1);
    return v;
  }
};

}  // namespace pmm
