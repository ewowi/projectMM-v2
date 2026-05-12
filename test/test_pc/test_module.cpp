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
