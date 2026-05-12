#pragma once
//
// PixelSource — interface for modules that produce a frame of RGB pixels.
//
// The shape carries w/h/d so 1D strips, 2D panels, and 3D volumes are all
// the same type. Sprint 6 ships 16×16×1 (a panel), but consumers iterate
// `count()` pixels regardless of layout. Consumers that care about geometry
// (e.g. the preview's binary frame header) read width/height/depth directly.
//
// `revision` lets consumers detect when the buffer pointer or dimensions
// changed (e.g. user edited `width` on the effect). Consumers compare against
// the value they last saw; if different, they recompute any per-buffer
// derived state (universe count for Art-Net, packet templates, etc.). Per
// pixel reads themselves don't pay any cost — the revision check is one
// uint32 compare on entry to the consumer's tick.
//
// Effects inherit `MoonModule` + `PixelSource` via multiple inheritance.
// Consumers locate sources through PixelRegistry (publish/find by id) —
// arduino-esp32 builds with -fno-rtti so dynamic_cast is unavailable; the
// registry sidesteps that entirely without putting anything in core.
//

#include <cstdint>

#include "RGB.h"

namespace pmm {

struct PixelBufferRef {
  const RGB* data;
  uint16_t   width;
  uint16_t   height;
  uint16_t   depth;
  uint32_t   revision;

  uint32_t count() const { return (uint32_t)width * height * depth; }
  bool     valid() const { return data != nullptr && count() > 0; }
};

class PixelSource {
 public:
  virtual ~PixelSource() = default;
  virtual PixelBufferRef pixelBuffer() const = 0;
};

}  // namespace pmm
