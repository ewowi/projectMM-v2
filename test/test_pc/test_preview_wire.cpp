// PreviewModule wire-format byte-for-byte tests.
// PreviewModule's loop20ms broadcasts a binary frame whose layout the
// frontend's renderPixelFrame in app.js depends on:
//
//   byte  0:    0x02   magic / version
//   bytes 1-2:  uint16 width  (little-endian)
//   bytes 3-4:  uint16 height (LE)
//   bytes 5-6:  uint16 depth  (LE)
//   bytes 7..:  RGB[width * height * depth]
//
// The packer is exposed static (PreviewModule::pack_frame) so we can assert
// the bytes here without spinning up a real WebSocket server.

#include <cstdint>
#include <vector>

#include <doctest/doctest.h>

#include "modules/lights/Pixelable.h"
#include "modules/lights/PreviewModule.h"
#include "modules/lights/RGB.h"

using namespace pmm;

TEST_CASE("PreviewModule wire format: 4x4 frame header + body bytes") {
  RGB pixels[16];
  // Tag each pixel with a distinct R so we can verify the body order:
  // row-major (y outer, x inner) is what the frontend renders.
  for (uint8_t i = 0; i < 16; ++i) pixels[i] = {(uint8_t)(i + 10), 0, 0};

  const PixelBufferRef ref = { pixels, 4, 4, 1, /*revision=*/0 };
  std::vector<uint8_t> dst(PreviewModule::required_frame_bytes(ref));
  REQUIRE(dst.size() == 7u + 16u * 3u);

  const size_t n = PreviewModule::pack_frame(dst.data(), ref);
  REQUIRE(n == dst.size());

  CHECK(dst[0] == 0x02);                  // magic
  CHECK(dst[1] == 0x04); CHECK(dst[2] == 0x00);  // width  = 4 LE
  CHECK(dst[3] == 0x04); CHECK(dst[4] == 0x00);  // height = 4 LE
  CHECK(dst[5] == 0x01); CHECK(dst[6] == 0x00);  // depth  = 1 LE
  // Body: 16 RGB triplets in source order.
  for (uint8_t i = 0; i < 16; ++i) {
    CHECK(dst[7 + i * 3 + 0] == (uint8_t)(i + 10));
    CHECK(dst[7 + i * 3 + 1] == 0);
    CHECK(dst[7 + i * 3 + 2] == 0);
  }
}

TEST_CASE("PreviewModule wire format: width and height LE encoding for 128x128") {
  // Single non-zero byte to keep the test fast; the geometry header is the
  // load-bearing part for a 128x128 panel (matches Sprint 7 stress test).
  std::vector<RGB> pixels(128 * 128, RGB{1, 2, 3});
  const PixelBufferRef ref = { pixels.data(), 128, 128, 1, 0 };
  std::vector<uint8_t> dst(PreviewModule::required_frame_bytes(ref));
  PreviewModule::pack_frame(dst.data(), ref);

  CHECK(dst[0] == 0x02);
  CHECK(dst[1] == 0x80); CHECK(dst[2] == 0x00);  // 128 LE = 0x80 0x00
  CHECK(dst[3] == 0x80); CHECK(dst[4] == 0x00);
  CHECK(dst[5] == 0x01); CHECK(dst[6] == 0x00);
  CHECK(dst.size() == 7u + 128u * 128u * 3u);
}
