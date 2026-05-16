// Behavioral tests for RipplesEffect's LUT hot path.
// Verifies the contract added in Sprint 7's perf tune:
//   - geometry is driven by a linked GridLayoutModule; changes reallocate + bump revision
//   - hue_base changes recolour without touching the phase table
//   - loop20ms() with the LUT path produces non-uniform, deterministic-ish
//     pixel output (one byte per channel, within [0, 255], not all zero,
//     not all the same).

#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

#include <ArduinoJson.h>
#include <doctest/doctest.h>

#include "core/DataRegistry.h"
#include "core/ModuleManager.h"
#include "modules/lights/GridLayoutModule.h"
#include "modules/lights/Pixelable.h"
#include "modules/lights/RipplesEffect.h"

using namespace pmm;

namespace {

// Add a GridLayoutModule + RipplesEffect linked to it.
// Returns the RipplesEffect pointer.
RipplesEffect* add_layout_and_ripples_(ModuleManager& mm,
                                       const char* layout_id,
                                       const char* ripples_id) {
  mm.register_type<GridLayoutModule>("layout");
  mm.register_type<RipplesEffect>("ripples");
  mm.add("layout",  layout_id);
  auto* r = dynamic_cast<RipplesEffect*>(mm.add("ripples", ripples_id));
  if (r) {
    JsonDocument doc;
    doc.set(layout_id);
    r->setControl("layout", JsonVariantConst(doc));
  }
  return r;
}

}  // namespace

TEST_CASE("RipplesEffect: lifecycle adds and exposes a valid pixel buffer") {
  ModuleManager mm;
  mm.register_type<RipplesEffect>("ripples");
  auto* r = dynamic_cast<RipplesEffect*>(mm.add("ripples", "r0"));
  REQUIRE(r != nullptr);
  const PixelBufferRef ref = r->pixelBuffer();
  CHECK(ref.valid());
  CHECK(ref.width  == 16);  // fallback default — no layout linked
  CHECK(ref.height == 16);
  CHECK(ref.depth  == 1);
  CHECK(ref.count() == 16 * 16);
}

TEST_CASE("RipplesEffect: geometry change via GridLayoutModule reallocates and bumps revision") {
  ModuleManager mm;
  auto* r = add_layout_and_ripples_(mm, "layout-0", "r0");
  REQUIRE(r != nullptr);
  const uint32_t rev0 = r->pixelBuffer().revision;

  auto* layout = dynamic_cast<GridLayoutModule*>(mm.find("layout-0"));
  REQUIRE(layout != nullptr);
  REQUIRE(layout->setControl("width",  32.0f));
  REQUIRE(layout->setControl("height", 32.0f));
  // Geometry change on the layout must trigger reallocation in the effect.
  r->runSetup();  // re-resolves layout and reallocates

  const PixelBufferRef ref = r->pixelBuffer();
  CHECK(ref.valid());
  CHECK(ref.width  == 32);
  CHECK(ref.height == 32);
  CHECK(ref.revision > rev0);
}

TEST_CASE("RipplesEffect: loop20ms writes non-trivial pixel output") {
  ModuleManager mm;
  auto* r = add_layout_and_ripples_(mm, "layout-0", "r0");
  REQUIRE(r != nullptr);

  auto* layout = dynamic_cast<GridLayoutModule*>(mm.find("layout-0"));
  REQUIRE(layout != nullptr);
  REQUIRE(layout->setControl("width",  32.0f));
  REQUIRE(layout->setControl("height", 32.0f));
  r->runSetup();

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
  CHECK(distinct > 0);
  CHECK((sum_r + sum_g + sum_b) > 0);
}

TEST_CASE("RipplesEffect: hue_base change recolours the output") {
  ModuleManager mm;
  mm.register_type<RipplesEffect>("ripples");
  auto* r = dynamic_cast<RipplesEffect*>(mm.add("ripples", "r0"));
  REQUIRE(r != nullptr);
  REQUIRE(r->setControl("hue_base", 0.0f));
  r->runLoop20ms();

  const PixelBufferRef ref0 = r->pixelBuffer();
  REQUIRE(ref0.data != nullptr);
  std::vector<RGB> snap_at_red(ref0.data, ref0.data + ref0.count());

  REQUIRE(r->setControl("hue_base", 127.0f));
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
  CHECK(differ > (ref1.count() * 9 / 10));
}

TEST_CASE("RipplesEffect: DataRegistry declares buffer for consumers to find") {
  ModuleManager mm;
  mm.register_type<RipplesEffect>("ripples");
  auto* r = dynamic_cast<RipplesEffect*>(mm.add("ripples", "r-published"));
  REQUIRE(r != nullptr);
  const DataBufferEntry* e = DataRegistry::instance().resolve("r-published");
  CHECK(e != nullptr);
  CHECK(e->buf_ptr != nullptr);
  CHECK(e->dim[0] == 16);   // fallback default width
  CHECK(e->dim[1] == 16);   // fallback default height
}
