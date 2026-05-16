// Scenario replay tests — Sprint 8 Rail 3.
//
// Every JSON under test/test_pc/scenarios/ is parsed and replayed in-process
// via ModuleManager. Steps: `add_module` (id, type) and `set_control` (id,
// key, value). Steps marked `"measure": true` get a `[scenario] measure …`
// log line + optional bounds assertion. Rail 2's [MemBoot] / [MemLive]
// events fire as modules pass through runSetup, so the test stdout shows the
// same memory trail a real boot does — exactly the "events in the ui.py log
// window" consumption pattern the user asked for.
//
// Adding a new scenario does NOT require new test code: drop the JSON, the
// glob loop picks it up. v1 ran the same JSON in two runtime contexts (in-
// process + REST against device); v2 starts with just the in-process side
// and earns the REST runner only when a drift episode unlocks it.

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <ArduinoJson.h>
#include <doctest/doctest.h>

#include "core/ModuleManager.h"
#include "modules/lights/ArtnetOutModule.h"
#include "modules/lights/PreviewModule.h"
#include "modules/lights/GridLayoutModule.h"
#include "modules/lights/RipplesEffect.h"
#include "modules/system/SystemStatusModule.h"

namespace fs = std::filesystem;
using namespace pmm;

namespace {

void register_scenario_types_(ModuleManager& mm) {
  mm.register_type<GridLayoutModule>("layout");
  mm.register_type<RipplesEffect>("ripples");
  mm.register_type<PreviewModule>("preview");
  mm.register_type<ArtnetOutModule>("artnet-out");
  mm.register_type<SystemStatusModule>("system");
}

std::string read_text_(const fs::path& p) {
  std::ifstream f(p);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// Walk all modules + remove via mm.remove() so PixelRegistry stops referencing
// the soon-to-be-deleted instances (mm's default dtor doesn't recurse into
// runTeardown).
void teardown_all_(ModuleManager& mm) {
  while (mm.size() > 0) mm.remove(mm.at(mm.size() - 1)->id());
}

void check_bounds_(JsonObjectConst bounds, const ModuleManager& mm,
                   const char* step_name) {
  if (bounds.isNull()) return;
  JsonVariantConst mc = bounds["module_count"];
  if (!mc.isNull()) {
    INFO("step: " << step_name);
    if (mc["min"].is<int>()) CHECK(mm.size() >= (size_t)mc["min"].as<int>());
    if (mc["max"].is<int>()) CHECK(mm.size() <= (size_t)mc["max"].as<int>());
  }
}

void replay_scenario_(const fs::path& path) {
  const std::string body = read_text_(path);
  REQUIRE_FALSE(body.empty());
  JsonDocument doc;
  REQUIRE(deserializeJson(doc, body) == DeserializationError::Ok);

  const char* name = doc["name"] | path.stem().c_str();
  std::printf("[scenario] %s\n", name);

  ModuleManager mm;
  register_scenario_types_(mm);

  for (JsonVariantConst step : doc["steps"].as<JsonArrayConst>()) {
    const char* op    = step["op"]   | "";
    const char* sname = step["name"] | op;

    if (std::strcmp(op, "add_module") == 0) {
      const char* type = step["type"] | "";
      const char* id   = step["id"]   | "";
      INFO("step: " << sname);
      REQUIRE(mm.add(type, id) != nullptr);
    } else if (std::strcmp(op, "set_control") == 0) {
      const char* id  = step["id"]  | "";
      const char* key = step["key"] | "";
      MoonModule* m   = mm.find(id);
      INFO("step: " << sname);
      REQUIRE(m != nullptr);
      CHECK(m->setControl(key, JsonVariantConst(step["value"])));
    } else {
      INFO("unknown op: " << op);
      FAIL("scenario step has no recognised op");
    }

    if (step["measure"] | false) {
      std::printf("  measure %s  (modules=%u)\n", sname, (unsigned)mm.size());
      check_bounds_(step["bounds"].as<JsonObjectConst>(), mm, sname);
    }
  }

  teardown_all_(mm);
}

}  // namespace

TEST_CASE("Scenarios: every JSON in test/test_pc/scenarios/ replays clean") {
  const fs::path dir = "test/test_pc/scenarios";
  REQUIRE(fs::is_directory(dir));

  unsigned count = 0;
  for (const auto& entry : fs::directory_iterator(dir)) {
    if (entry.path().extension() != ".json") continue;
    ++count;
    // Each scenario gets its own SUBCASE so a failure in one doesn't mask
    // others; the SUBCASE name is the file stem for easy localisation.
    const std::string stem = entry.path().stem().string();
    SUBCASE(stem.c_str()) {
      replay_scenario_(entry.path());
    }
  }
  CHECK(count > 0);  // at least one scenario must exist
}
