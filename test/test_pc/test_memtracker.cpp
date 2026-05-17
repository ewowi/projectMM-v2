// MemTracker + classSize drift tests (v1 test_memory_stats + test_techdebt
// migration, port-and-minimized for v2 on PC).
//
// Port-and-minimize note: on PC pal::free_heap_kb()/max_alloc_kb() return 0
// (no real heap introspection — see PalHeap.h / the v1 unittest.py note).
// So these assert *invariants and consistency*, not absolute KB values:
// snapshot() is callable and self-consistent, frag_pct stays in range, and
// every registered module's classSize() equals sizeof(T) (the v1 techdebt
// sweep that catches a factory/footprint drift).

#include <doctest/doctest.h>

#include <string>

#include "core/ModuleManager.h"
#include "modules/system/MemTracker.h"
#include "modules/system/SystemStatusModule.h"
#include "modules/lights/GridLayoutModule.h"
#include "modules/lights/RipplesEffect.h"
#include "modules/lights/PreviewModule.h"
#include "modules/lights/ArtnetOutModule.h"

using namespace pmm;

TEST_CASE("memtracker: snapshot() is callable and internally consistent") {
  const auto a = memtracker::snapshot();
  const auto b = memtracker::snapshot();
  // largest free block can never exceed total free heap (PC: both 0 — the
  // invariant 0 <= largest <= free still holds and must not underflow).
  CHECK(a.largest_kb <= a.free_heap_kb);
  CHECK(b.largest_kb <= b.free_heap_kb);
  // Two back-to-back snapshots with no allocation in between are stable.
  CHECK(a.free_heap_kb == b.free_heap_kb);
}

TEST_CASE("memtracker: frag_pct stays within 0..100 and is 0 on a 0 heap") {
  memtracker::Snapshot s{};
  s.free_heap_kb = 0; s.largest_kb = 0;
  CHECK(memtracker::frag_pct_(s) == 0u);     // PC path: no divide-by-zero
  s.free_heap_kb = 100; s.largest_kb = 60;
  const unsigned f = memtracker::frag_pct_(s);
  CHECK(f <= 100u);
  // 1 - 60/100 = 0.4 → *100 → 39.999f → (unsigned) truncates to 39
  // (v2 truncates, it does not round — assert the actual behavior).
  CHECK(f == 39u);
  // No fragmentation when the largest block IS the whole free heap.
  s.largest_kb = 100;
  CHECK(memtracker::frag_pct_(s) == 0u);
}

// v1 test_techdebt: every registered type's factory-injected classSize()
// must equal sizeof(T). Catches a module growing past what register_type<T>
// recorded (footprint reporting silently wrong).
TEST_CASE("techdebt: classSize() == sizeof(T) for every registered module type") {
  ModuleManager mm;
  mm.register_type<SystemStatusModule>("system");
  mm.register_type<GridLayoutModule>("layout");
  mm.register_type<RipplesEffect>("ripples");
  mm.register_type<PreviewModule>("preview");
  mm.register_type<ArtnetOutModule>("artnet-out");

  CHECK(mm.add("system",     "a")->classSize() == sizeof(SystemStatusModule));
  CHECK(mm.add("layout",     "b")->classSize() == sizeof(GridLayoutModule));
  CHECK(mm.add("ripples",    "c")->classSize() == sizeof(RipplesEffect));
  CHECK(mm.add("preview",    "d")->classSize() == sizeof(PreviewModule));
  CHECK(mm.add("artnet-out", "e")->classSize() == sizeof(ArtnetOutModule));
}

TEST_CASE("techdebt: an unregistered type is a base MoonModule sized accordingly") {
  ModuleManager mm;
  MoonModule* m = mm.add("unknown", "u");
  // Base MoonModule via the no-factory path: classSize is 0 (never injected
  // by register_type<T>) — documents v2's actual behavior, not a v1 guess.
  CHECK(m->classSize() == 0);
  CHECK(std::string(m->type()) == "unknown");
}
