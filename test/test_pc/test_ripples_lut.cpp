// Behavioral tests for RipplesEffect's LUT hot path.
// Verifies the contract added in Sprint 7's perf tune:
//   - geometry changes (width/height/depth) reallocate pixels + bump revision
//   - hue_base changes recolour without touching the phase table
//   - loop20ms() with the LUT path produces non-uniform, deterministic-ish
//     pixel output (one byte per channel, within [0, 255], not all zero,
//     not all the same).

#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

#include <doctest/doctest.h>

#include "core/DataRegistry.h"
#include "core/ModuleManager.h"
#include "modules/lights/Pixelable.h"
#include "modules/lights/RipplesEffect.h"

using namespace pmm;

namespace {

RipplesEffect* add_ripples_(ModuleManager& mm, const char* id) {
  mm.register_type<RipplesEffect>("ripples");
  return dynamic_cast<RipplesEffect*>(mm.add("ripples", id));
}

}  // namespace

TEST_CASE("RipplesEffect: lifecycle adds and exposes a valid pixel buffer") {
  ModuleManager mm;
  RipplesEffect* r = add_ripples_(mm, "r0");
  REQUIRE(r != nullptr);
  const PixelBufferRef ref = r->pixelBuffer();
  CHECK(ref.valid());
  CHECK(ref.width  == 16);
  CHECK(ref.height == 16);
  CHECK(ref.depth  == 1);
  CHECK(ref.count() == 16 * 16);
}

TEST_CASE("RipplesEffect: geometry change reallocates and bumps revision") {
  ModuleManager mm;
  RipplesEffect* r = add_ripples_(mm, "r0");
  REQUIRE(r != nullptr);
  const uint32_t rev0 = r->pixelBuffer().revision;

  REQUIRE(r->setControl("width",  32.0f));
  REQUIRE(r->setControl("height", 32.0f));

  const PixelBufferRef ref = r->pixelBuffer();
  CHECK(ref.valid());
  CHECK(ref.width  == 32);
  CHECK(ref.height == 32);
  CHECK(ref.revision > rev0);   // bumped at least once per geometry change
}

TEST_CASE("RipplesEffect: loop20ms writes non-trivial pixel output") {
  ModuleManager mm;
  RipplesEffect* r = add_ripples_(mm, "r0");
  REQUIRE(r != nullptr);
  REQUIRE(r->setControl("width",  32.0f));
  REQUIRE(r->setControl("height", 32.0f));

  // Run a few frames to advance the phase scalar so the wave is not at zero.
  for (int i = 0; i < 5; ++i) {
    r->runLoop20ms();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  const PixelBufferRef ref = r->pixelBuffer();
  REQUIRE(ref.data != nullptr);
  REQUIRE(ref.count() == 32 * 32);

  unsigned distinct = 0;
  uint8_t  prev_r   = ref.data[0].r;
  unsigned sum_r = 0, sum_g = 0, sum_b = 0;
  for (uint32_t i = 0; i < ref.count(); ++i) {
    sum_r += ref.data[i].r;
    sum_g += ref.data[i].g;
    sum_b += ref.data[i].b;
    if (ref.data[i].r != prev_r) ++distinct;
  }
  CHECK(distinct > 0);                            // pixels are not all the same red value
  CHECK((sum_r + sum_g + sum_b) > 0);             // something was written
}

TEST_CASE("RipplesEffect: hue_base change recolours the output") {
  // Snapshot frame at hue_base=0, then again at hue_base=0.5, then count
  // pixels whose RGB differs by more than just-time-drift noise. Sprint 7's
  // LUT path keeps base_color_ as a separate table that rebuild_color_table_
  // refreshes on hue_base change — this is the assertion that proves the
  // table actually flips when the control moves.
  ModuleManager mm;
  RipplesEffect* r = add_ripples_(mm, "r0");
  REQUIRE(r != nullptr);
  REQUIRE(r->setControl("hue_base", 0.0f));   // hue_base is uint8: 0 = hue 0.0
  r->runLoop20ms();

  const PixelBufferRef ref0 = r->pixelBuffer();
  REQUIRE(ref0.data != nullptr);
  std::vector<RGB> snap_at_red(ref0.data, ref0.data + ref0.count());

  REQUIRE(r->setControl("hue_base", 127.0f));  // uint8 127 ≈ hue 0.5 (complementary)
  r->runLoop20ms();

  const PixelBufferRef ref1 = r->pixelBuffer();
  unsigned differ = 0;
  for (uint32_t i = 0; i < ref1.count(); ++i) {
    if (snap_at_red[i].r != ref1.data[i].r ||
        snap_at_red[i].g != ref1.data[i].g ||
        snap_at_red[i].b != ref1.data[i].b) {
      ++differ;
    }
  }
  // Effectively every pixel should have re-coloured (hue rotated by 0.5 is
  // the complementary half of the wheel). Allow a small fudge for any pixel
  // whose brightness happens to be 0 at both snapshots.
  CHECK(differ > (ref1.count() * 9 / 10));
}

TEST_CASE("RipplesEffect: DataRegistry declares buffer for consumers to find") {
  ModuleManager mm;
  RipplesEffect* r = add_ripples_(mm, "r-published");
  REQUIRE(r != nullptr);
  const DataBufferEntry* e = DataRegistry::instance().resolve("r-published");
  CHECK(e != nullptr);
  CHECK(e->buf_ptr != nullptr);
  CHECK(e->dim[0] == 16);   // default width
  CHECK(e->dim[1] == 16);   // default height
}
