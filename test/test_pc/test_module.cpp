#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

int main(int argc, char** argv) {
  doctest::Context ctx(argc, argv);
  ctx.setOption("success", true);   // always report passed cases, not just failures
  ctx.setOption("no-version", true);
  return ctx.run();
}
#include <memory>
#include <string>

#include "core/ModuleManager.h"
#include "modules/system/SystemStatusModule.h"

using namespace pmm;

TEST_CASE("ModuleManager: add base module") {
  ModuleManager mm;
  MoonModule* m = mm.add("noop", "n0");
  CHECK(m != nullptr);
  CHECK(std::string(m->id()) == "n0");
  CHECK(std::string(m->type()) == "noop");
  CHECK(mm.size() == 1);
}

TEST_CASE("ModuleManager: factory builds concrete type") {
  ModuleManager mm;
  mm.register_type<SystemStatusModule>("system");
  MoonModule* m = mm.add("system", "s0");
  CHECK(m != nullptr);
  CHECK(dynamic_cast<SystemStatusModule*>(m) != nullptr);
  CHECK(std::string(m->type()) == "system");
  CHECK(m->classSize() == sizeof(SystemStatusModule));  // factory injected sizeof(T)
}

TEST_CASE("ModuleManager: manager pointer set on add") {
  ModuleManager mm;
  MoonModule* m = mm.add("noop", "n0");
  CHECK(m->manager() == &mm);
}

TEST_CASE("ModuleManager: remove existing") {
  ModuleManager mm;
  mm.add("noop", "n0");
  CHECK(mm.remove("n0"));
  CHECK(mm.size() == 0);
}

TEST_CASE("ModuleManager: remove non-existent") {
  ModuleManager mm;
  CHECK_FALSE(mm.remove("ghost"));
}

TEST_CASE("ModuleManager: find") {
  ModuleManager mm;
  mm.add("noop", "n0");
  CHECK(mm.find("n0") != nullptr);
  CHECK(mm.find("n0") == mm.at(0));
  CHECK(mm.find("x") == nullptr);
}

// --- v1 test_module_manager migration (port-and-minimize) -----------------
// v1 had duplicate-id-rejected / removeModule-HasChildren / replaceModule
// cases. v2's ModuleManager has NONE of those APIs by design: add() never
// rejects, remove() returns plain bool + recursively erases the subtree,
// there is no replace(). Duplicate-id rejection lives at the HTTP layer
// (409 in HttpServerModule) and is covered there, not here. So the only
// v2-actual ModuleManager behavior still untested is unknown-type handling
// and recursive subtree removal — those are what's migrated.

TEST_CASE("ModuleManager: unknown type yields a base module with a truthful type()") {
  ModuleManager mm;
  MoonModule* m = mm.add("totally-unregistered", "u0");
  REQUIRE(m != nullptr);
  CHECK(std::string(m->type()) == "totally-unregistered");  // not "" / not crash
  CHECK(std::string(m->id()) == "u0");
  CHECK(mm.size() == 1);
}

// (No standalone subtree-remove case here: a `noop` module has no input so
// it can't be nested, making such a test indistinguishable from flat
// remove. Real parent-detach + subtree-delete coverage lives in
// test_reparent.cpp with properly-inputted modules — not duplicated.)

// --- v1 test_system_info / test_stateful_module migration ------------------
// SystemStatusModule is a real v2 module with zero unit coverage today.

TEST_CASE("SystemStatusModule: setup/loop/teardown lifecycle does not crash") {
  ModuleManager mm;
  mm.register_type<SystemStatusModule>("system");
  auto* s = dynamic_cast<SystemStatusModule*>(mm.add("system", "sys-0"));
  REQUIRE(s != nullptr);
  s->runLoop();
  s->runLoop1s();
  s->runLoop10s();
  CHECK(mm.remove("sys-0"));   // teardown via remove()
}

TEST_CASE("SystemStatusModule: exposes named controls and classSize == sizeof") {
  ModuleManager mm;
  mm.register_type<SystemStatusModule>("system");
  auto* s = mm.add("system", "sys-0");
  REQUIRE(s != nullptr);
  CHECK(s->classSize() == sizeof(SystemStatusModule));  // factory-injected
  CHECK(s->controlCount() > 0);                          // onBuildControls ran
  CHECK(s->hasControl("local_time"));                    // a known display control
}

TEST_CASE("SystemStatusModule: getSchema after loop1s sampling is well-formed") {
  ModuleManager mm;
  mm.register_type<SystemStatusModule>("system");
  auto* s = mm.add("system", "sys-0");
  s->runLoop1s();   // sample dynamic fields
  JsonDocument doc;
  s->getSchema(doc.to<JsonObject>());
  CHECK(std::string(doc["id"] | "") == "sys-0");
  CHECK(!doc["controls"].isNull());
}
