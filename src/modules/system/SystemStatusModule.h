#pragma once
//
// SystemStatusModule — exposes runtime system metrics as live display controls.
//
// Ported from v1's modules/system/SystemStatus.h (198 LOC) with the v2 lifecycle:
//   - setup()           — one-time reads (chip_model, mac_address, totals)
//   - onBuildControls() — all addControl() calls (per the §4 refinement; v1
//                         put them in setup())
//   - loop()            — ++tickCount_ for fps measurement
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
    tickCount_ = 0;
    totalHeapKb_  = pal::total_heap_kb();
    totalPsramKb_ = pal::total_psram_kb();
    totalFsKb_    = pal::fs_total_kb();
    fsUsedKb_     = pal::fs_used_kb();
    pal::chip_model(chipModel_, sizeof(chipModel_));
    pal::mac_address(macAddress_, sizeof(macAddress_));
  }

  void onBuildControls() override {
    clearControls();  // no-op first call, necessary on rebuild
    const float heapMax = totalHeapKb_;
    const float fsMax   = totalFsKb_;

    addControl(localTime_, "local_time", "display");
    addControl(uptimeSec_, "uptime_s", "time", 0.0f, 2'000'000.0f);
    addControl(fps_,       "fps",      "display", 0.0f, 2'000'000.0f);

    addControl(heapUsedKb_,     "heap_used_kb",       "progress", 0.0f, heapMax);
    addControl(heapFreeKb_,     "heap_free_kb",       "display",  0.0f, 524288.0f);
    addControl(maxAllocUsedKb_, "max_alloc_used_kb",  "progress", 0.0f, heapMax);
    addControl(heapMinKb_,      "heap_min_kb",        "display",  0.0f, 524288.0f);
    addControl(maxAllocKb_,     "max_alloc_kb",       "display",  0.0f, heapMax);
    addControl(fragPct_,        "heap_frag_pct",      "display",  0.0f, 100.0f);

    if (totalPsramKb_ > 0) {
      addControl(psramUsedKb_,   "psram_used_kb",  "progress", 0.0f, totalPsramKb_);
      addControl(totalPsramKb_,  "total_psram_kb", "display",  0.0f, totalPsramKb_);
      addControl(psramFreeKb_,   "psram_free_kb",  "display",  0.0f, totalPsramKb_);
      addControl(pal::psram_mode(), "psram_mode", "display");
    }

    addControl(fsUsedKb_, "fs_used_kb", "progress", 0.0f, fsMax);
    addControl(fsFreeKb_, "fs_free_kb", "display",  0.0f, fsMax);

    addControl(pal::sketch_kb(),    "firmware_kb", "progress", 0.0f, pal::sketch_partition_kb());
    addControl(coreTemp_,           "core_temp",   "display", -40.0f, 125.0f);
    addControl(pal::reset_reason_str(), "reset_reason", "display");

    addControl(APP_VERSION,  "firmware_version", "display");
    addControl(BUILD_TARGET, "env",              "display");
    addControl(BUILD_DATE,   "build_date",       "display");
    addControl(BUILD_TIME,   "build_time",       "display");
    addControl(pal::platform_version(), "platform_version", "display");
    addControl(pal::sdk_version(),      "sdk_version",      "display");

    addControl(pal::cpu_freq_mhz(), "cpu_freq_mhz", "display", 0.0f, 1000.0f);
    addControl(pal::cpu_cores(),    "cpu_cores",    "display", 0.0f, 8.0f);

    addControl(chipModel_,  "chip_model",  "display");
    addControl(macAddress_, "mac_address", "display");

    addControl(pal::flash_size_mb(),   "flash_size_mb",   "display", 0.0f, 256.0f);
    addControl(pal::flash_speed_mhz(), "flash_speed_mhz", "display", 0.0f, 160.0f);
    addControl(pal::flash_chip_mode(), "flash_chip_mode", "display");
  }

  // loop() counts hot-path ticks for fps measurement — bounded, no alloc.
  void loop() override { ++tickCount_; }

  // loop1s() samples dynamic fields once a second, keeping loop() near-zero cost.
  void loop1s() override {
    const int64_t nowUs = (int64_t)pal::micros();
    const float elapsed = (float)(nowUs - lastSampleUs_);
    if (elapsed > 0) {
      fps_ = (tickCount_ - lastTick_) * 1e6f / elapsed;
      lastTick_ = tickCount_;
    }
    lastSampleUs_ = nowUs;

    pal::local_time_str(localTime_, sizeof(localTime_));
    uptimeSec_ = (float)((nowUs - startUs_) / 1000000LL);

    heapFreeKb_     = pal::free_heap_kb();
    maxAllocKb_     = pal::max_alloc_kb();
    maxAllocUsedKb_ = (totalHeapKb_ > 0.0f ? totalHeapKb_ : heapFreeKb_) - maxAllocKb_;
    heapMinKb_      = pal::heap_min_kb();
    fragPct_        = heapFreeKb_ > 0 ? 100.0f - maxAllocKb_ * 100.0f / heapFreeKb_ : 0.0f;
    coreTemp_       = pal::core_temp();
    heapUsedKb_     = totalHeapKb_ - heapFreeKb_;
    if (totalPsramKb_ > 0) {
      psramFreeKb_ = pal::free_psram_kb();
      psramUsedKb_ = totalPsramKb_ - psramFreeKb_;
    }
    fsUsedKb_ = pal::fs_used_kb();
    fsFreeKb_ = totalFsKb_ - fsUsedKb_;
  }

  // Sprint 5 /api/system: contribute structured runtime metrics.
  void fillSystemJson(JsonObject out) const override {
    out["system_fps"]    = fps_;
    out["uptime_s"]      = uptimeSec_;
    out["heap_free_kb"]  = heapFreeKb_;
    out["heap_used_kb"]  = heapUsedKb_;
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
    if (chipModel_[0])  out["chip_model"]  = chipModel_;
    if (macAddress_[0]) out["mac_address"] = macAddress_;
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
  float fps_            = 0.0f;
  float uptimeSec_      = 0.0f;
  float heapFreeKb_     = 0.0f;
  float heapMinKb_      = 0.0f;
  float maxAllocKb_     = 0.0f;
  float maxAllocUsedKb_ = 0.0f;
  float fragPct_        = 0.0f;
  float coreTemp_       = 0.0f;
  float heapUsedKb_     = 0.0f;

  // -- Captured once in setup() ---------------------------------------------
  float totalHeapKb_  = 0.0f;
  float totalPsramKb_ = 0.0f;
  float psramFreeKb_  = 0.0f;
  float psramUsedKb_  = 0.0f;
  float totalFsKb_    = 0.0f;
  float fsUsedKb_     = 0.0f;
  float fsFreeKb_     = 0.0f;

  char localTime_[12]  = "--:--:--";
  char chipModel_[32]  = {};
  char macAddress_[18] = {};

  int64_t startUs_      = 0;
  int64_t lastSampleUs_ = 0;
  uint32_t tickCount_   = 0;
  uint32_t lastTick_    = 0;
};

}  // namespace pmm
