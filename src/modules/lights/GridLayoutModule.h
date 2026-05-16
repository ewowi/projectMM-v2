#pragma once
//
// GridLayoutModule — sole authority on panel geometry and physical pixel wiring.
//
// Provides:
//   width(), height(), depth()  — 3D bounding box (not every point is a pixel)
//   physical_count()            — explicit pixel count (may be < w*h*d)
//   map(logical_idx)            — logical → physical index remapping
//
// Phase 1: rectangular panels, optional serpentine row wiring.
//   physical_count() == width * height * depth.
//   Serpentine: odd rows run right-to-left, reversing x within each row.
//
// Effects resolve this module by ID via their `layout` control and call
// these methods from onAllocateMemory() to size their buffers. No pixel
// data lives here; this module is stateless between ticks.
//

#include <cstring>

#include "../../core/MoonModule.h"

namespace pmm {

class GridLayoutModule : public MoonModule {
 public:
  const char* category() const override { return "Gridlayout"; }

  void onBuildControls() override {
    addControl(width_,      "width",      "slider", (uint16_t)1, (uint16_t)512);
    addControl(height_,     "height",     "slider", (uint16_t)1, (uint16_t)512);
    addControl(depth_,      "depth",      "slider", (uint16_t)1, (uint16_t)16);
    addControl(serpentine_, "serpentine", "toggle");
  }

  void onUpdate(const char* /*key*/) override {
    // Geometry changed — any linked effect must re-resolve and reallocate.
    // That is triggered by the effect's own onUpdate("layout") when it
    // polls us; nothing to do here beyond letting the new values be read.
  }

  // -- Geometry API (called by effects from onAllocateMemory) ----------------

  uint16_t width()  const { return width_;  }
  uint16_t height() const { return height_; }
  uint16_t depth()  const { return depth_;  }

  // Phase 1: physical count == w * h * d (no sparse layouts yet).
  uint32_t physical_count() const {
    return (uint32_t)width_ * height_ * depth_;
  }

  // Map a logical pixel index to a physical pixel index.
  // logical_idx is in row-major order (x + y*w + z*w*h).
  // With serpentine, odd rows run right-to-left within each depth slice.
  uint32_t map(uint32_t logical_idx) const {
    if (!serpentine_) return logical_idx;
    const uint32_t plane = (uint32_t)width_ * height_;
    const uint32_t z     = logical_idx / plane;
    const uint32_t rem   = logical_idx % plane;
    const uint32_t y     = rem / width_;
    const uint32_t x     = rem % width_;
    const uint32_t px    = (y & 1u) ? (width_ - 1u - x) : x;
    return z * plane + y * width_ + px;
  }

 private:
  uint16_t width_      = 16;
  uint16_t height_     = 16;
  uint16_t depth_      = 1;
  bool     serpentine_ = false;
};

}  // namespace pmm
