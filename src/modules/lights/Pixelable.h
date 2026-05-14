#pragma once
//
// PixelSource — interface for modules that produce a frame of RGB pixels.
//
// The shape carries w/h/d so 1D strips, 2D panels, and 3D volumes are all
// the same type. Consumers iterate `count()` pixels regardless of layout.
// Consumers that care about geometry (preview wire header, Art-Net universe
// count) read width/height/depth directly.
//
// Sprint 13: pixel data lives in a DataRing<RGB> owned by DataRegistry,
// not in the effect module. PixelBufferRef wraps a pointer into the ring
// slot; the ring is resolved by consumers via DataRegistry::resolve(id).
// PixelSource::pixelBuffer() is kept as a same-core fast path (no registry
// lookup — the effect caches the ring pointer and fills the ref inline).
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

  // Returns a ref into the most-recently published ring slot.
  // Hot-path cost: virtual call + one atomic load (revision).
  virtual PixelBufferRef pixelBuffer() const = 0;
};

}  // namespace pmm
