// Round-trip integration test for StateStoreModule.
// Boot 1: configure a module, change a control, trigger snapshot write.
// Boot 2: fresh ModuleManager. StateStoreModule's setup reads /modules.json
// and per-module state files, re-instantiates the persisted module with the
// stashed control value.
//
// Filesystem sandbox: PalFs maps the firmware paths "/modules.json" and
// "/state-<id>.json" to ./state/* under CWD. Cleanup happens before and
// after each TEST_CASE so the suite is order-independent.

#include <cstdint>
#include <filesystem>
#include <system_error>

#include <doctest/doctest.h>

#include "core/ModuleManager.h"
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
    mm.register_type<RipplesEffect>("ripples");
    mm.register_type<StateStoreModule>("state-store");

    MoonModule* store = mm.add("state-store", "state-store-0");
    REQUIRE(store != nullptr);
    // state-store's setup snapshots an empty list; ripples will appear as
    // "new" on the next loop10s pass.

    auto* ripples = dynamic_cast<RipplesEffect*>(mm.add("ripples", "r-roundtrip"));
    REQUIRE(ripples != nullptr);
    REQUIRE(ripples->setControl("width",  64.0f));
    REQUIRE(ripples->setControl("height", 64.0f));

    store->runLoop10s();   // triggers the snapshot write

    CHECK(fs::exists("state/modules.json"));
    CHECK(fs::exists("state/state-r-roundtrip.json"));

    // Manual teardown so PixelRegistry stops referencing the about-to-die
    // RipplesEffect (ModuleManager's default destructor doesn't recurse
    // into runTeardown — only the explicit remove() path does).
    mm.remove("r-roundtrip");
    mm.remove("state-store-0");
  }

  // ── Boot 2: fresh manager re-creates from disk ──────────────────────────
  {
    ModuleManager mm;
    mm.register_type<RipplesEffect>("ripples");
    mm.register_type<StateStoreModule>("state-store");

    // state-store's setup reads /modules.json and re-adds the persisted
    // ripples-r-roundtrip with its saved control values pre-applied.
    MoonModule* store = mm.add("state-store", "state-store-0");
    REQUIRE(store != nullptr);

    auto* restored = dynamic_cast<RipplesEffect*>(mm.find("r-roundtrip"));
    REQUIRE(restored != nullptr);
    const PixelBufferRef ref = restored->pixelBuffer();
    CHECK(ref.width  == 64);
    CHECK(ref.height == 64);

    mm.remove("r-roundtrip");
    mm.remove("state-store-0");
  }

  clean_state_dir_();
}
