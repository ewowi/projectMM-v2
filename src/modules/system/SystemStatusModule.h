#pragma once
//
// SystemStatusModule — exposes runtime system metrics as live display controls.
//
// Ported from v1's modules/system/SystemStatus.h (198 LOC) with the v2 lifecycle:
//   - setup()           — one-time reads (chip_model, mac_address, totals)
//   - onBuildControls() — all addControl() calls (per the §4 refinement; v1
//                         put them in setup())
//   - loop()            — ++fpsTick_ for fps measurement
//   - loop1s()          — sample dynamic fields (heap, temp, time, fps)
//
// On ESP32 (Sprint 4): real values via pal::*; PC shows the stubs in
// PalSystemInfo.h (0 / "" for hardware-specific fields). The module
// itself has ZERO platform conditionals (enforced by check_platform_guards).
//

#include <cstdint>

#include "../../core/MoonModule.h"
#include "../../pal/Pal.h"
#include "../../pal/PalSystemInfo.h"
#include "MemTracker.h"

// Build-time identification — `scripts/inject_build_info.py` generates
// `BuildInfo.h` at the start of every PlatformIO build and adds its dir
// to CPPPATH, so BUILD_DATE / BUILD_TIME refresh on every compile. The
// __DATE__/__TIME__ fallback only fires if the script didn't run (e.g.,
// editor "go to definition" with no project build active).
#if __has_include("BuildInfo.h")
  #include "BuildInfo.h"
#endif
#ifndef APP_VERSION
  #define APP_VERSION "v2-dev"
#endif
#ifndef BUILD_TARGET
  #define BUILD_TARGET "pc"
#endif
#ifndef BUILD_DATE
  #define BUILD_DATE __DATE__
#endif
#ifndef BUILD_TIME
  #define BUILD_TIME __TIME__
#endif

namespace pmm {

class SystemStatusModule : public MoonModule {
 public:
  const char* category() const override { return "system"; }

  void setup() override {
    startUs_ = (int64_t)pal::micros();
    lastSampleUs_ = startUs_;
    lastTick_ = 0;
    fpsTick_ = 0;
    totalHeapKb_  = pal::total_heap_kb();
    totalPsramKb_ = pal::total_psram_kb();
    totalFsKb_    = pal::fs_total_kb();
    fsUsedKb_     = pal::fs_used_kb();
    pal::chip_model_str();   // warm the static cache at setup time
    pal::mac_address_str();
  }

  void onBuildControls() override {
    clearControls();  // no-op first call, necessary on rebuild

    addControl(localTime_, "local_time", "display");
    addControl(uptimeSec_, "uptime_s", "time", 0u, 2'000'000u);
    addControl((uint16_t)fps_, "fps",   "display");

    addControl(heapUsedKb_,     "heap_used_kb",       "progress", 0u, totalHeapKb_);
    addControl(heapFreeKb_,     "heap_free_kb",       "display",  0u, totalHeapKb_);
    addControl(maxAllocUsedKb_, "max_alloc_used_kb",  "progress", 0u, totalHeapKb_);
    addControl(heapMinKb_,      "heap_min_kb",        "display",  0u, totalHeapKb_);
    addControl(maxAllocKb_,     "max_alloc_kb",       "display",  0u, totalHeapKb_);
    addControl(fragPct_,        "heap_frag_pct",      "display",  (uint8_t)0, (uint8_t)100);

    if (totalPsramKb_ > 0) {
      addControl(psramUsedKb_,  "psram_used_kb",  "progress", 0u, totalPsramKb_);
      addControl(totalPsramKb_, "total_psram_kb", "display",  0u, totalPsramKb_);
      addControl(psramFreeKb_,  "psram_free_kb",  "display",  0u, totalPsramKb_);
      addControl(pal::psram_mode(), "psram_mode", "display");
    }

    addControl(fsUsedKb_, "fs_used_kb", "progress", 0u, totalFsKb_);
    addControl(fsFreeKb_, "fs_free_kb", "display",  0u, totalFsKb_);

    addControl(pal::sketch_kb(),         "firmware_kb", "progress", 0u, pal::sketch_partition_kb());
    addControl((int8_t)coreTemp_,        "core_temp",   "display",  (int8_t)-40, (int8_t)125);
    addControl(pal::reset_reason_str(),  "reset_reason", "display");

    addControl(APP_VERSION,  "firmware_version", "display");
    addControl(BUILD_TARGET, "env",              "display");
    addControl(BUILD_DATE,   "build_date",       "display");
    addControl(BUILD_TIME,   "build_time",       "display");
    addControl(pal::platform_version(), "platform_version", "display");
    addControl(pal::sdk_version(),      "sdk_version",      "display");

    addControl(pal::cpu_freq_mhz(), "cpu_freq_mhz", "display");
    addControl(pal::cpu_cores(),    "cpu_cores",    "display");

    addControl(pal::chip_model_str(),  "chip_model",  "display");
    addControl(pal::mac_address_str(), "mac_address", "display");

    addControl(pal::flash_size_kb(),   "flash_size_kb",   "display");
    addControl(pal::flash_speed_mhz(), "flash_speed_mhz", "display");
    addControl(pal::flash_chip_mode(), "flash_chip_mode", "display");
  }

  // loop() counts hot-path ticks for fps measurement — bounded, no alloc.
  void loop() override { ++fpsTick_; }

  // loop1s() samples dynamic fields once a second, keeping loop() near-zero cost.
  void loop1s() override {
    const int64_t nowUs   = (int64_t)pal::micros();
    const int64_t elapsed = nowUs - lastSampleUs_;
    if (elapsed > 0) {
      fps_ = (uint16_t)((fpsTick_ - lastTick_) * 1000000LL / elapsed);
      lastTick_ = fpsTick_;
    }
    lastSampleUs_ = nowUs;

    pal::local_time_str(localTime_, sizeof(localTime_));
    uptimeSec_ = (uint32_t)((nowUs - startUs_) / 1000000LL);

    heapFreeKb_     = pal::free_heap_kb();
    maxAllocKb_     = pal::max_alloc_kb();
    maxAllocUsedKb_ = (totalHeapKb_ > 0 ? totalHeapKb_ : heapFreeKb_) - maxAllocKb_;
    heapMinKb_      = pal::heap_min_kb();
    fragPct_        = heapFreeKb_ > 0 ? (uint8_t)(100 - maxAllocKb_ * 100 / heapFreeKb_) : 0;
    coreTemp_       = pal::core_temp();
    heapUsedKb_     = totalHeapKb_ - heapFreeKb_;
    if (totalPsramKb_ > 0) {
      psramFreeKb_ = pal::free_psram_kb();
      psramUsedKb_ = totalPsramKb_ - psramFreeKb_;
    }
    fsUsedKb_ = pal::fs_used_kb();
    memtracker::tick_1s();
    fsFreeKb_ = totalFsKb_ - fsUsedKb_;
  }

  // Sprint 5 /api/system: contribute structured runtime metrics.
  void fillSystemJson(JsonObject out) const override {
    out["system_fps"]    = fps_;
    out["uptime_s"]      = uptimeSec_;
    out["heap_free_kb"]   = heapFreeKb_;
    out["heap_used_kb"]   = heapUsedKb_;
    out["max_alloc_kb"]   = maxAllocKb_;
    out["core_temp"]     = coreTemp_;
    if (totalPsramKb_ > 0) {
      out["psram_free_kb"]  = psramFreeKb_;
      out["psram_total_kb"] = totalPsramKb_;
      out["psram_mode"]     = pal::psram_mode();
    }
    if (totalFsKb_ > 0) {
      out["fs_used_kb"]  = fsUsedKb_;
      out["fs_total_kb"] = totalFsKb_;
    }
    if (pal::chip_model_str()[0])  out["chip_model"]  = pal::chip_model_str();
    if (pal::mac_address_str()[0]) out["mac_address"] = pal::mac_address_str();
    out["firmware_version"] = APP_VERSION;
    out["env"]              = BUILD_TARGET;
    out["build_date"]       = BUILD_DATE;
    out["build_time"]       = BUILD_TIME;
    out["platform_version"] = pal::platform_version();
    out["reset_reason"]     = pal::reset_reason_str();
    out["is_crash"]         = pal::is_crash_reset();
  }

 private:
  // -- Dynamic samples (refreshed in loop1s) --------------------------------
  uint16_t fps_            = 0;
  uint32_t uptimeSec_      = 0;
  uint32_t heapFreeKb_     = 0;
  uint32_t heapMinKb_      = 0;
  uint32_t maxAllocKb_     = 0;
  uint32_t maxAllocUsedKb_ = 0;
  uint8_t  fragPct_        = 0;
  int8_t   coreTemp_       = 0;
  uint32_t heapUsedKb_     = 0;

  // -- Captured once in setup() ---------------------------------------------
  uint32_t totalHeapKb_  = 0;
  uint32_t totalPsramKb_ = 0;
  uint32_t psramFreeKb_  = 0;
  uint32_t psramUsedKb_  = 0;
  uint32_t totalFsKb_    = 0;
  uint32_t fsUsedKb_     = 0;
  uint32_t fsFreeKb_     = 0;

  char localTime_[12] = "--:--:--";

  int64_t startUs_      = 0;
  int64_t lastSampleUs_ = 0;
  uint32_t fpsTick_   = 0;
  uint32_t lastTick_    = 0;
};

}  // namespace pmm
