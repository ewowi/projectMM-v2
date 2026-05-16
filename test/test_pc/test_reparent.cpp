// Sprint 17 — the parent IS an input. reparent by name-match, reject on no
// matching input, promote keeps the value, derived tree + loop order, nested
// reorder. (Supersedes the Sprint 16 auto-wire/parent_ tests.)

#include <cstring>
#include <string>

#include <doctest/doctest.h>

#include "core/ModuleManager.h"
#include "core/MoonModule.h"

using namespace pmm;

namespace {

// Child with one EditStr input named "layout" — nests under a "layout" type.
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

// Child with NO inputs — can never be a child (no input to flag as parent).
class NoInputModule : public MoonModule {
 public:
  const char* category() const override { return "test"; }
};

// Child with a generic wildcard "source" input (like preview/artnet) — nests
// under ANY parent type, since "source" matches any parent.
class WildcardModule : public MoonModule {
 public:
  const char* category() const override { return "test"; }
  void onBuildControls() override {
    addControl(src_buf_, sizeof(src_buf_), "source", "text");
  }
  const char* src() const { return src_buf_; }
 private:
  char src_buf_[24] = "";
};

// Child with BOTH a name-matchable "layout" input and a wildcard "source" —
// exact name==type must win over the generic source.
class DualModule : public MoonModule {
 public:
  const char* category() const override { return "test"; }
  void onBuildControls() override {
    addControl(src_buf_,    sizeof(src_buf_),    "source", "text");
    addControl(layout_buf_, sizeof(layout_buf_), "layout", "text");
  }
  const char* src() const    { return src_buf_; }
  const char* layout() const { return layout_buf_; }
 private:
  char src_buf_[24]    = "";
  char layout_buf_[24] = "";
};

// Parent module — type registered as "layout".
class SourceModule : public MoonModule {
 public:
  const char* category() const override { return "test"; }
};

}  // namespace

TEST_CASE("reparent: parent flag set on the name-matched input") {
  ModuleManager mm;
  mm.register_type<SourceModule>("layout");
  mm.register_type<SinkModule>("sink");

  MoonModule* parent = mm.add("layout", "layout-0");
  auto* child = dynamic_cast<SinkModule*>(mm.add("sink", "sink-0"));
  REQUIRE(child != nullptr);

  REQUIRE(mm.reparent("sink-0", "layout-0"));
  CHECK(child->parent() == parent);
  REQUIRE(parent->childCount() == 1);
  CHECK(parent->child(0) == child);
  // The relationship IS the input: value written, flag points at it.
  CHECK(std::string(child->src()) == "layout-0");
  CHECK(child->parentControlIdx() >= 0);
  CHECK(std::string(child->parentInputKey()) == "layout");
}

TEST_CASE("reparent: rejected when no input is named for the parent type") {
  ModuleManager mm;
  mm.register_type<SourceModule>("layout");
  mm.register_type<NoInputModule>("bare");

  mm.add("layout", "layout-0");
  MoonModule* bare = mm.add("bare", "bare-0");

  // No input named "layout" (no inputs at all) → drop rejected.
  CHECK_FALSE(mm.reparent("bare-0", "layout-0"));
  CHECK(bare->parent() == nullptr);
  CHECK(bare->parentControlIdx() == -1);
}

TEST_CASE("reparent: wildcard 'source' input matches any parent type") {
  ModuleManager mm;
  mm.register_type<SourceModule>("ripples");   // parent type != "source"
  mm.register_type<WildcardModule>("preview");

  MoonModule* parent = mm.add("ripples", "ripples-0");
  auto* prev = dynamic_cast<WildcardModule*>(mm.add("preview", "preview-0"));
  REQUIRE(prev != nullptr);

  // No input named "ripples", but "source" is the wildcard → accepted.
  REQUIRE(mm.reparent("preview-0", "ripples-0"));
  CHECK(prev->parent() == parent);
  CHECK(std::string(prev->src()) == "ripples-0");
  CHECK(std::string(prev->parentInputKey()) == "source");
}

TEST_CASE("reparent: exact name==type wins over generic 'source'") {
  ModuleManager mm;
  mm.register_type<SourceModule>("layout");
  mm.register_type<DualModule>("dual");

  mm.add("layout", "layout-0");
  auto* d = dynamic_cast<DualModule*>(mm.add("dual", "dual-0"));
  REQUIRE(d != nullptr);

  // Has both "source" and "layout"; parent type is "layout" → exact wins.
  REQUIRE(mm.reparent("dual-0", "layout-0"));
  CHECK(std::string(d->parentInputKey()) == "layout");
  CHECK(std::string(d->layout()) == "layout-0");
  CHECK(std::string(d->src()) == "");          // wildcard untouched
}

TEST_CASE("reparent: promote to root clears the flag but KEEPS the value") {
  ModuleManager mm;
  mm.register_type<SourceModule>("layout");
  mm.register_type<SinkModule>("sink");

  mm.add("layout", "layout-0");
  auto* sink = dynamic_cast<SinkModule*>(mm.add("sink", "sink-0"));
  REQUIRE(sink != nullptr);

  mm.reparent("sink-0", "layout-0");
  REQUIRE(std::string(sink->src()) == "layout-0");
  REQUIRE(sink->parentControlIdx() >= 0);

  mm.reparent("sink-0", "");  // promote to root
  CHECK(sink->parent() == nullptr);
  CHECK(sink->parentControlIdx() == -1);          // flag cleared
  CHECK(std::string(sink->src()) == "layout-0");  // value KEPT (data-flow link)
}

TEST_CASE("reparent: loop order — parent precedes child after reparent") {
  ModuleManager mm;
  mm.register_type<SourceModule>("layout");
  mm.register_type<SinkModule>("sink");

  mm.add("sink",   "sink-0");
  mm.add("layout", "layout-0");
  CHECK(std::string(mm.at(0)->id()) == "sink-0");

  mm.reparent("sink-0", "layout-0");
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

  mm.reorder({"sink-b", "sink-a"}, "layout-0");
  CHECK(std::string(parent->child(0)->id()) == "sink-b");
  CHECK(std::string(parent->child(1)->id()) == "sink-a");

  CHECK(std::string(mm.at(0)->id()) == "layout-0");
  CHECK(std::string(mm.at(1)->id()) == "sink-b");
  CHECK(std::string(mm.at(2)->id()) == "sink-a");
}
