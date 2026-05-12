#pragma once
//
// RGB — 24-bit colour primitive for the lighting domain.
//
// Lives in modules/lights/ (per CLAUDE.md: lighting primitives belong with
// their modules, not in core). Trivial layout: byte-packed r/g/b, no padding,
// so a `RGB[N]` is exactly 3N bytes — the same layout the Art-Net DMX wire
// format and the frontend's preview-frame body both expect.
//
// Helpers (clear, fromHsv) are inline + branch-light because effects call
// them per-pixel in loop20ms. No allocations, no heap touches.
//

#include <cstdint>
#include <cmath>

namespace pmm {

struct RGB {
  uint8_t r;
  uint8_t g;
  uint8_t b;

  static constexpr RGB black() { return {0, 0, 0}; }

  // Hue (0..1, wraps), saturation (0..1), value (0..1) → RGB. Standard
  // 6-segment HSV→RGB. Used by RipplesEffect for hue-modulated brightness.
  static RGB fromHsv(float h, float s, float v) {
    h = h - std::floor(h);                   // wrap to [0, 1)
    const float c = v * s;
    const float x = c * (1.0f - std::fabs(std::fmod(h * 6.0f, 2.0f) - 1.0f));
    const float m = v - c;
    float r1 = 0, g1 = 0, b1 = 0;
    const int sector = (int)(h * 6.0f) % 6;
    switch (sector) {
      case 0: r1 = c; g1 = x; b1 = 0; break;
      case 1: r1 = x; g1 = c; b1 = 0; break;
      case 2: r1 = 0; g1 = c; b1 = x; break;
      case 3: r1 = 0; g1 = x; b1 = c; break;
      case 4: r1 = x; g1 = 0; b1 = c; break;
      default:r1 = c; g1 = 0; b1 = x; break;
    }
    return { (uint8_t)((r1 + m) * 255.0f),
             (uint8_t)((g1 + m) * 255.0f),
             (uint8_t)((b1 + m) * 255.0f) };
  }
};

static_assert(sizeof(RGB) == 3, "RGB must be byte-packed for wire-format compatibility");

}  // namespace pmm
