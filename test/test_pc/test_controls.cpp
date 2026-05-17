// MoonModule control-system unit tests (v1 test_controls_and_kv +
// test_stateful_module, ported to v2's actual contract). v2 was previously
// only testing controls transitively (via ripples / state-store); this is
// the direct unit test of setControl / onUpdate / getSchema /
// getControlValues / clearControls / select controls.
//
// Port-and-minimize note: v1 had a "setControl clamps out-of-range" case.
// v2's writeThrough_ does NOT clamp (it casts straight through —
// MoonModule.cpp). That is v2's deliberate behavior, so no clamp test
// exists here; testing a clamp v2 doesn't do would assert a false contract.

#include <doctest/doctest.h>

#include <string>

#include <ArduinoJson.h>

#include "core/ModuleManager.h"

using namespace pmm;

namespace {

// Exercises every writable control kind + an onUpdate counter so we can
// assert the callback fires exactly on a recognised key.
class CtlModule : public MoonModule {
 public:
  const char* category() const override { return "test"; }
  void onBuildControls() override {
    clearControls();
    addControl(f_,    "f",    "slider", 0.0f, 10.0f);
    addControl(u8_,   "u8",   "slider", (uint8_t)0,  (uint8_t)255);
    addControl(u32_,  "u32",  "slider", (uint32_t)0, (uint32_t)1000);
    addControl(b_,    "b",    "toggle");
    addControl(mode_, "mode", kModes, kModeCount);          // select
    addControl(name_, sizeof(name_), "name", "text");        // EditStr
  }
  void onUpdate(const char* key) override { ++updates_; last_ = key; }

  float    f_   = 1.5f;
  uint8_t  u8_  = 7;
  uint32_t u32_ = 42;
  bool     b_   = false;
  uint8_t  mode_ = 0;
  char     name_[16] = "init";
  int         updates_ = 0;
  std::string last_;

  static constexpr const char* kModes[] = {"a", "b", "c"};
  static constexpr uint8_t kModeCount = 3;
};

MoonModule* add_ctl(ModuleManager& mm) {
  mm.register_type<CtlModule>("ctl");
  return mm.add("ctl", "ctl-0");
}

}  // namespace

TEST_CASE("setControl float writes through and fires onUpdate") {
  ModuleManager mm;
  auto* m = static_cast<CtlModule*>(add_ctl(mm));
  REQUIRE(m->setControl("f", 3.25f));
  CHECK(m->f_ == doctest::Approx(3.25f));
  CHECK(m->updates_ == 1);
  CHECK(m->last_ == "f");
}

TEST_CASE("setControl uint8/uint32/bool write through (cast, no clamp)") {
  ModuleManager mm;
  auto* m = static_cast<CtlModule*>(add_ctl(mm));
  CHECK(m->setControl("u8",  200.0f)); CHECK(m->u8_  == 200);
  CHECK(m->setControl("u32", 999.0f)); CHECK(m->u32_ == 999u);
  CHECK(m->setControl("b",   1.0f));   CHECK(m->b_   == true);
  CHECK(m->setControl("b",   0.0f));   CHECK(m->b_   == false);
}

TEST_CASE("setControl unknown key is a no-op returning false") {
  ModuleManager mm;
  auto* m = static_cast<CtlModule*>(add_ctl(mm));
  CHECK_FALSE(m->setControl("nope", 1.0f));
  CHECK(m->updates_ == 0);  // onUpdate NOT called for an unknown key
}

TEST_CASE("setControl via JsonVariant takes the REST path") {
  ModuleManager mm;
  auto* m = static_cast<CtlModule*>(add_ctl(mm));
  JsonDocument d; d.set(5.0f);
  CHECK(m->setControl("f", d.as<JsonVariantConst>()));
  CHECK(m->f_ == doctest::Approx(5.0f));
}

TEST_CASE("EditStr control is writable via JsonVariant, not the float path") {
  ModuleManager mm;
  auto* m = static_cast<CtlModule*>(add_ctl(mm));
  CHECK_FALSE(m->setControl("name", 1.0f));   // float path rejects EditStr
  JsonDocument d; d.set("hello");
  CHECK(m->setControl("name", d.as<JsonVariantConst>()));
  CHECK(std::string(m->name_) == "hello");
}

TEST_CASE("getSchema emits id + a control entry per addControl with min/max/default") {
  ModuleManager mm;
  auto* m = static_cast<CtlModule*>(add_ctl(mm));
  JsonDocument doc;
  m->getSchema(doc.to<JsonObject>());
  CHECK(std::string(doc["id"] | "") == "ctl-0");
  JsonArray ctrls = doc["controls"];
  REQUIRE(!ctrls.isNull());
  // enabled_ (system) + 6 declared = 7
  CHECK(ctrls.size() >= 7);
  bool saw_f = false;
  for (JsonObject c : ctrls)
    if (std::string(c["key"] | "") == "f") {
      saw_f = true;
      CHECK(c["min"].as<float>() == doctest::Approx(0.0f));
      CHECK(c["max"].as<float>() == doctest::Approx(10.0f));
    }
  CHECK(saw_f);
}

TEST_CASE("select control: getSchema carries the options array + index value") {
  ModuleManager mm;
  auto* m = static_cast<CtlModule*>(add_ctl(mm));
  CHECK(m->setControl("mode", 2.0f));
  CHECK(m->mode_ == 2);
  JsonDocument doc;
  m->getSchema(doc.to<JsonObject>());
  for (JsonObject c : doc["controls"].as<JsonArray>())
    if (std::string(c["key"] | "") == "mode") {
      JsonArray opts = c["options"];
      REQUIRE(!opts.isNull());
      CHECK(opts.size() == 3);
      CHECK(std::string(opts[0] | "") == "a");
    }
}

TEST_CASE("getControlValues is a flat {key:value} reflecting live fields") {
  ModuleManager mm;
  auto* m = static_cast<CtlModule*>(add_ctl(mm));
  m->setControl("f", 8.0f);
  m->setControl("u8", 9.0f);
  JsonDocument doc;
  m->getControlValues(doc.to<JsonObject>());
  CHECK(doc["f"].as<float>() == doctest::Approx(8.0f));
  CHECK((doc["u8"] | 0) == 9);
  // a second change is reflected on the next read
  m->setControl("f", 2.0f);
  JsonDocument doc2;
  m->getControlValues(doc2.to<JsonObject>());
  CHECK(doc2["f"].as<float>() == doctest::Approx(2.0f));
}

TEST_CASE("clearControls + rebuild preserves the live value (pending props)") {
  ModuleManager mm;
  auto* m = static_cast<CtlModule*>(add_ctl(mm));
  m->setControl("f", 7.0f);
  m->onBuildControls();          // starts with clearControls() then re-adds
  CHECK(m->hasControl("f"));
  CHECK(m->f_ == doctest::Approx(7.0f));   // value survived the rebuild
}

TEST_CASE("schemaDirty: clearControls marks dirty, clearSchemaDirty resets it") {
  ModuleManager mm;
  auto* m = static_cast<CtlModule*>(add_ctl(mm));
  m->clearControls();
  CHECK(m->schemaDirty());
  m->clearSchemaDirty();
  CHECK_FALSE(m->schemaDirty());
}

TEST_CASE("defVal is captured from the field initializer, not a later setControl") {
  ModuleManager mm;
  auto* m = static_cast<CtlModule*>(add_ctl(mm));
  m->setControl("f", 9.0f);                 // change the live value
  JsonDocument doc;
  m->getSchema(doc.to<JsonObject>());
  for (JsonObject c : doc["controls"].as<JsonArray>())
    if (std::string(c["key"] | "") == "f")
      CHECK(c["default"].as<float>() == doctest::Approx(1.5f));  // still the ctor init, not 9
}
