// Scheduler core-affinity tests. Verifies that Scheduler::core_loop dispatches
// each module only to the core whose id matches the module's coreAffinity().
// The Sprint 7 wiring replaced `i % cores` with `m->coreAffinity() != core_id
// → continue`; these tests prove that contract on PC.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

#include <doctest/doctest.h>

#include "core/ModuleManager.h"
#include "core/MoonModule.h"
#include "core/Scheduler.h"

using namespace pmm;

namespace {

class CounterModule : public MoonModule {
 public:
  CounterModule() = default;
  explicit CounterModule(uint8_t core) { setCoreAffinity(core); }
  void loop() override { ticks.fetch_add(1, std::memory_order_relaxed); }
  std::atomic<uint64_t> ticks{0};
};

// Run sched in a background thread for `ms` ms, then stop it.
//
// Two-core runs spawn an extra std::thread via pal::task_create_pinned that
// PalRtos *detaches* on PC (production never returns from run() on the
// device, so detach is the right call there). That detached thread reads
// `sched` until it observes stop_; we have to give it time to exit before
// the Scheduler goes out of scope, or the second test in this file races
// the first's stale thread → SIGSEGV. 50 ms is generous — each core_loop
// iteration is microseconds — but cheap and deterministic.
void run_briefly_(Scheduler& sched, int ms) {
  std::thread t([&] { sched.run(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
  sched.stop();
  t.join();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

}  // namespace

TEST_CASE("Scheduler affinity: two-core scenario ticks both modules") {
  ModuleManager mm;
  mm.register_type<CounterModule>("c0", uint8_t{0});
  mm.register_type<CounterModule>("c1", uint8_t{1});
  auto* m0 = dynamic_cast<CounterModule*>(mm.add("c0", "core0-mod"));
  auto* m1 = dynamic_cast<CounterModule*>(mm.add("c1", "core1-mod"));
  REQUIRE(m0 != nullptr);
  REQUIRE(m1 != nullptr);
  CHECK(m0->coreAffinity() == 0);
  CHECK(m1->coreAffinity() == 1);

  Scheduler sched(&mm, /*cores=*/2);
  run_briefly_(sched, 30);

  CHECK(m0->ticks.load() > 0);
  CHECK(m1->ticks.load() > 0);
}

TEST_CASE("Scheduler affinity: single-core scenario ignores out-of-range modules") {
  // With cores=1, only core_id 0 runs. A module pinned to core 1 must NOT
  // be ticked — proves the loop filter is `affinity == core_id`, not
  // `affinity < cores`.
  ModuleManager mm;
  mm.register_type<CounterModule>("c0", uint8_t{0});
  mm.register_type<CounterModule>("c1", uint8_t{1});
  auto* m0 = dynamic_cast<CounterModule*>(mm.add("c0", "core0-mod"));
  auto* m1 = dynamic_cast<CounterModule*>(mm.add("c1", "core1-mod"));
  REQUIRE(m0 != nullptr);
  REQUIRE(m1 != nullptr);

  Scheduler sched(&mm, /*cores=*/1);
  run_briefly_(sched, 30);

  CHECK(m0->ticks.load() > 0);
  CHECK(m1->ticks.load() == 0);  // load-bearing: core 1 was never spawned
}
