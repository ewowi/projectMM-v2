#pragma once
//
// LayoutModule — abstract base for panel-geometry modules.
//
// A layout answers category()=="layout" and exposes a 3D bounding box plus a
// logical→physical map. Effects link to "a layout" (PixelEffectBase resolves
// by category(), never by the registered factory type — the v1
// strcmp-on-type-name pattern). Concrete layouts: GridLayoutModule (rect /
// serpentine), RingLayoutModule (circle), WheelLayoutModule (radial spokes).
//
// width/height/depth are virtual so PixelEffectBase can read geometry through
// a LayoutModule* without knowing which concrete layout it linked. map() has
// a passthrough default (logical == physical) for rectangular layouts; sparse
// layouts override it.
//

#include "../../../core/MoonModule.h"

namespace pmm {

class LayoutModule : public MoonModule {
 public:
  const char* category() const override { return "layout"; }

  virtual uint16_t width()  const = 0;
  virtual uint16_t height() const = 0;
  virtual uint16_t depth()  const = 0;

  // Physical pixel count; default is the dense bounding box.
  virtual uint32_t physical_count() const {
    return (uint32_t)width() * height() * depth();
  }

  // Logical pixel index → physical index. Default: identity (dense rect).
  virtual uint32_t map(uint32_t logical_idx) const { return logical_idx; }
};

}  // namespace pmm
