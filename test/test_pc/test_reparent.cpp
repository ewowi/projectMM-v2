// Sprint 16 Step 1 — reparent, auto-wire, loop order, nested reorder.

#include <cstring>
#include <string>

#include <doctest/doctest.h>

#include "core/ModuleManager.h"
#include "core/MoonModule.h"

using namespace pmm;

namespace {

// Minimal module with one EditStr control whose key matches "parent-type".
class SinkModule : public MoonModule {
 public:
  const char* category() const override { return "test"; }
  void onBuildControls() override {
    addControl(src_buf_, sizeof(src_buf_), "layout", "text");
  }
  const char* src() const { return src_buf_; }
 private:
  char src_buf_[24] = "";
};

// Minimal parent module — type registered as "layout".
class SourceModule : public MoonModule {
 public:
  const char* category() const override { return "test"; }
};

}  // namespace

TEST_CASE("reparent: module becomes child of parent") {
  ModuleManager mm;
  mm.register_type<SourceModule>("layout");
  mm.register_type<SinkModule>("sink");

  MoonModule* parent = mm.add("layout", "layout-0");
  MoonModule* child  = mm.add("sink",   "sink-0");

  REQUIRE(mm.reparent("sink-0", "layout-0"));
  CHECK(child->parent() == parent);
  // parent's children array contains the child
  REQUIRE(parent->childCount() == 1);
  CHECK(parent->child(0) == child);
}

TEST_CASE("reparent: auto-wire fires — matching EditStr control set to parent id") {
  ModuleManager mm;
  mm.register_type<SourceModule>("layout");
  mm.register_type<SinkModule>("sink");

  mm.add("layout", "layout-0");
  auto* sink = dynamic_cast<SinkModule*>(mm.add("sink", "sink-0"));
  REQUIRE(sink != nullptr);

  mm.reparent("sink-0", "layout-0");
  CHECK(std::string(sink->src()) == "layout-0");
}

TEST_CASE("reparent: detach to root clears auto-wired control") {
  ModuleManager mm;
  mm.register_type<SourceModule>("layout");
  mm.register_type<SinkModule>("sink");

  mm.add("layout", "layout-0");
  auto* sink = dynamic_cast<SinkModule*>(mm.add("sink", "sink-0"));
  REQUIRE(sink != nullptr);

  mm.reparent("sink-0", "layout-0");
  REQUIRE(std::string(sink->src()) == "layout-0");

  mm.reparent("sink-0", "");  // detach → root
  CHECK(sink->parent() == nullptr);
  CHECK(std::string(sink->src()) == "");
}

TEST_CASE("reparent: loop order — parent precedes child in modules_ after reparent") {
  ModuleManager mm;
  mm.register_type<SourceModule>("layout");
  mm.register_type<SinkModule>("sink");

  // Add sink first so it's initially before layout in modules_.
  mm.add("sink",   "sink-0");
  mm.add("layout", "layout-0");
  // Before reparent: sink-0 is at index 0.
  CHECK(std::string(mm.at(0)->id()) == "sink-0");

  mm.reparent("sink-0", "layout-0");
  // After reparent: depth-first sort puts layout-0 before sink-0.
  CHECK(std::string(mm.at(0)->id()) == "layout-0");
  CHECK(std::string(mm.at(1)->id()) == "sink-0");
}

TEST_CASE("reparent: returns false for unknown id") {
  ModuleManager mm;
  CHECK_FALSE(mm.reparent("ghost", ""));
}

TEST_CASE("reorder children: nested reorder changes child order and loop order") {
  ModuleManager mm;
  mm.register_type<SourceModule>("layout");
  mm.register_type<SinkModule>("sink");

  mm.add("layout", "layout-0");
  mm.add("sink",   "sink-a");
  mm.add("sink",   "sink-b");
  mm.reparent("sink-a", "layout-0");
  mm.reparent("sink-b", "layout-0");

  MoonModule* parent = mm.find("layout-0");
  REQUIRE(parent != nullptr);
  REQUIRE(parent->childCount() == 2);
  CHECK(std::string(parent->child(0)->id()) == "sink-a");

  // Reorder: sink-b before sink-a.
  mm.reorder({"sink-b", "sink-a"}, "layout-0");
  CHECK(std::string(parent->child(0)->id()) == "sink-b");
  CHECK(std::string(parent->child(1)->id()) == "sink-a");

  // modules_ depth-first: layout-0, sink-b, sink-a.
  CHECK(std::string(mm.at(0)->id()) == "layout-0");
  CHECK(std::string(mm.at(1)->id()) == "sink-b");
  CHECK(std::string(mm.at(2)->id()) == "sink-a");
}
