#pragma once
//
// MemTracker — emit machine-parseable memory events to pmm::log so they flow
// through the existing ring → serial / /api/log / ui.py log window. No file
// is written; events are consumed live by humans or LLMs reading the log.
//
// Sprint 8 Rail 2. The line shape is intentionally stable so a reader can
// answer "did module X leak during the scenario?" without a custom parser:
//
//   [MemBoot] <id>      setup ±N.NKB = M.MKB (frag=P%, largest=L.LKB) [PR: ±X.XKB = Y.YKB]
//   [MemLive] <id>      setup ±N.NKB = M.MKB (...) [PR: ±X.XKB = Y.YKB]
//   [MemLive] periodic  =        M.MKB (...)                  ← every 10 s
//   [MemLive] delta     ±N.NKB = M.MKB (...)                  ← when heap moves > kThreshold
//
// Sign convention matches v1's deploy/run/*.log: the value after `=` is the
// current absolute free heap; the signed delta is `new_free - old_free`, so
// `-7.0KB` means free shrank by 7 KB (memory consumed) and `+14.5KB` means
// free grew by 14.5 KB (memory released). The PR suffix is PSRAM on ESP32;
// absent on PC (psram_kb returns 0 from PalSystemInfo there).
//
// MemBoot is taken at the end of MoonModule::runSetup *after setup() returns*
// — the setup() cost alone. MemLive is taken at end of runSetup, after
// children and onAllocateMemory — the total setup-phase cost including
// dynamic buffers and child trees. The pair brackets module setup so drift in
// either phase is visible.

#include <cstdint>

#include "../../pal/PalSystemInfo.h"
#include "Logger.h"

namespace pmm::memtracker {

struct Snapshot {
  uint32_t free_heap_kb;
  uint32_t largest_kb;
  uint32_t free_psram_kb;
};

inline Snapshot snapshot() {
  return { pal::free_heap_kb(), pal::max_alloc_kb(), pal::free_psram_kb() };
}

inline unsigned frag_pct_(const Snapshot& s) {
  if (s.free_heap_kb == 0) return 0u;
  const float frag = 1.0f - ((float)s.largest_kb / (float)s.free_heap_kb);
  return frag > 0.0f ? (unsigned)(frag * 100.0f) : 0u;
}

inline void log_pair_(const char* tag, const char* id, const Snapshot& before) {
  const Snapshot now  = snapshot();
  const int32_t  dkb  = (int32_t)now.free_heap_kb - (int32_t)before.free_heap_kb;
  if (now.free_psram_kb > 0 || before.free_psram_kb > 0) {
    const int32_t pr_dkb = (int32_t)now.free_psram_kb - (int32_t)before.free_psram_kb;
    pmm::log("%s %-20s setup %+5dKB = %6uKB (frag=%u%%, largest=%uKB) PR: %+dKB = %uKB\n",
             tag, id, dkb, now.free_heap_kb, frag_pct_(now), now.largest_kb,
             pr_dkb, now.free_psram_kb);
  } else {
    pmm::log("%s %-20s setup %+5dKB = %6uKB (frag=%u%%, largest=%uKB)\n",
             tag, id, dkb, now.free_heap_kb, frag_pct_(now), now.largest_kb);
  }
}

// Take a snapshot before calling setup(); pass the result here after setup()
// returns. Computes and logs `[MemBoot]` with the setup-only delta.
inline void log_boot(const char* id, const Snapshot& before) { log_pair_("[MemBoot]", id, before); }

// Same as log_boot but `[MemLive]`. Pass the same `before` snapshot so MemLive
// shows the *total* setup-phase delta (setup + onAllocateMemory + children).
inline void log_live(const char* id, const Snapshot& before) { log_pair_("[MemLive]", id, before); }

// 1 Hz heartbeat. Emits one of:
//   `[MemLive] delta ±N.NKB = M.MKB ...`  when |new - old| > kThresholdKb
//   `[MemLive] periodic = M.MKB ...`      every 10 s as an "alive" signal
// Threshold keeps the log quiet when nothing is happening — per the user's
// "serial output should only show events" stance in the Sprint 8 plan.
inline void tick_1s() {
  static uint32_t last_kb = 0;
  static uint32_t tick    = 0;
  static bool     seeded  = false;

  const Snapshot now = snapshot();
  if (!seeded) {                       // first call seeds without emitting
    last_kb = now.free_heap_kb;
    seeded  = true;
    ++tick;
    return;
  }

  constexpr uint32_t kThresholdKb = 1;
  const int32_t delta = (int32_t)now.free_heap_kb - (int32_t)last_kb;

  if (delta > (int32_t)kThresholdKb || delta < -(int32_t)kThresholdKb) {
    pmm::log("[MemLive] delta %+dKB = %uKB (frag=%u%%, largest=%uKB)\n",
             delta, now.free_heap_kb, frag_pct_(now), now.largest_kb);
    last_kb = now.free_heap_kb;
  } else if (tick % 10 == 0) {
    pmm::log("[MemLive] periodic = %uKB (frag=%u%%, largest=%uKB)\n",
             now.free_heap_kb, frag_pct_(now), now.largest_kb);
  }
  ++tick;
}

}  // namespace pmm::memtracker
