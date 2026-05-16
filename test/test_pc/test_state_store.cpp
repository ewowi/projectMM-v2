// Round-trip integration test for StateStoreModule.
// Boot 1: configure a module, change a control, trigger snapshot write.
// Boot 2: fresh ModuleManager. StateStoreModule's setup reads /modules.json
// and per-module state files, re-instantiates the persisted module with the
// stashed control value.
//
// Filesystem sandbox: PalFs maps the firmware paths "/modules.json" and
// "/state-<id>.json" to ./state/* under CWD. Cleanup happens before and
// after each TEST_CASE so the suite is order-independent.
//
// Geometry is set via a GridLayoutModule linked to RipplesEffect (Sprint 15).

#include <cstdint>
#include <filesystem>
#include <system_error>

#include <ArduinoJson.h>
#include <doctest/doctest.h>

#include "core/ModuleManager.h"
#include "modules/lights/GridLayoutModule.h"
#include "modules/lights/Pixelable.h"
#include "modules/lights/RipplesEffect.h"
#include "modules/system/StateStoreModule.h"

namespace fs = std::filesystem;
using namespace pmm;

namespace {

void clean_state_dir_() {
  std::error_code ec;
  fs::remove_all("state", ec);  // best-effort; missing dir is fine
}

}  // namespace

TEST_CASE("StateStoreModule round-trip — control survives a simulated reboot") {
  clean_state_dir_();

  // ── Boot 1: configure + persist ─────────────────────────────────────────
  {
    ModuleManager mm;
    mm.register_type<GridLayoutModule>("layout");
    mm.register_type<RipplesEffect>("ripples");
    mm.register_type<StateStoreModule>("state-store");

    MoonModule* store = mm.add("state-store", "state-store-0");
    REQUIRE(store != nullptr);

    auto* layout = dynamic_cast<GridLayoutModule*>(mm.add("layout", "layout-0"));
    REQUIRE(layout != nullptr);
    REQUIRE(layout->setControl("width",  64.0f));
    REQUIRE(layout->setControl("height", 64.0f));

    auto* ripples = dynamic_cast<RipplesEffect*>(mm.add("ripples", "r-roundtrip"));
    REQUIRE(ripples != nullptr);
    { JsonDocument d; d.set("layout-0"); REQUIRE(ripples->setControl("layout", JsonVariantConst(d))); }

    store->runLoop1s();    // triggers modules.json write (moved from loop10s)
    store->runLoop10s();   // triggers per-module state write

    CHECK(fs::exists("state/modules.json"));
    CHECK(fs::exists("state/state-r-roundtrip.json"));

    mm.remove("r-roundtrip");
    mm.remove("layout-0");
    mm.remove("state-store-0");
  }

  // ── Boot 2: fresh manager re-creates from disk ──────────────────────────
  {
    ModuleManager mm;
    mm.register_type<GridLayoutModule>("layout");
    mm.register_type<RipplesEffect>("ripples");
    mm.register_type<StateStoreModule>("state-store");

    // state-store's setup reads /modules.json and re-adds all persisted modules.
    MoonModule* store = mm.add("state-store", "state-store-0");
    REQUIRE(store != nullptr);

    auto* layout   = dynamic_cast<GridLayoutModule*>(mm.find("layout-0"));
    auto* restored = dynamic_cast<RipplesEffect*>(mm.find("r-roundtrip"));
    REQUIRE(layout   != nullptr);
    REQUIRE(restored != nullptr);

    CHECK(layout->width()  == 64);
    CHECK(layout->height() == 64);

    const PixelBufferRef ref = restored->pixelBuffer();
    CHECK(ref.width  == 64);
    CHECK(ref.height == 64);

    mm.remove("r-roundtrip");
    mm.remove("layout-0");
    mm.remove("state-store-0");
  }

  clean_state_dir_();
}
