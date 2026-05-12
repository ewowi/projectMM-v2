#pragma once
//
// PalSystemInfo — platform queries for chip identity, heap, filesystem,
// flash, CPU, reset reason, build/SDK versions, and wall-clock time.
// Used by modules/system/SystemStatusModule.
//
// Sprint 3: PC stubs return 0 / "" / sensible placeholders for the fields
// that don't have a meaningful PC equivalent (chip_model, mac_address,
// flash_*, etc.). Real values land in Sprint 4 alongside the ESP32 port
// of v1's pal::* implementations — at which point an `#ifdef ARDUINO`
// branch lands HERE in pal/, never in the modules that call these.
//

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <thread>

namespace pal {

// -- Wall-clock string ------------------------------------------------------
// HH:MM:SS in local time. PC: std::time + localtime_r + strftime.
inline void local_time_str(char* buf, size_t len) {
  if (!buf || len < 9) { if (buf && len) buf[0] = '\0'; return; }
  std::time_t t = std::time(nullptr);
  std::tm tm{};
  localtime_r(&t, &tm);
  std::strftime(buf, len, "%H:%M:%S", &tm);
}

// -- Chip identity ----------------------------------------------------------
inline void chip_model(char* buf, size_t len) {
  if (!buf || !len) return;
  std::strncpy(buf, "pc", len - 1);
  buf[len - 1] = '\0';
}
inline void mac_address(char* buf, size_t len) {
  if (!buf || !len) return;
  buf[0] = '\0';
}

// -- Reset reason -----------------------------------------------------------
inline const char* reset_reason_str() { return "boot"; }
inline bool        is_crash_reset()   { return false; }

// -- Heap (Sprint 4 fills in real PC values via mallinfo; ESP32 via ESP-IDF) -
inline float total_heap_kb() { return 0.0f; }
inline float free_heap_kb()  { return 0.0f; }
inline float max_alloc_kb()  { return 0.0f; }
inline float heap_min_kb()   { return 0.0f; }

// -- PSRAM (zero on PC by definition) ---------------------------------------
inline float       total_psram_kb() { return 0.0f; }
inline float       free_psram_kb()  { return 0.0f; }
inline const char* psram_mode()     { return "none"; }

// -- Filesystem (no LittleFS partition on PC) -------------------------------
inline float fs_total_kb() { return 0.0f; }
inline float fs_used_kb()  { return 0.0f; }

// -- Sketch / firmware size -------------------------------------------------
inline float sketch_kb()           { return 0.0f; }
inline float sketch_partition_kb() { return 0.0f; }

// -- CPU --------------------------------------------------------------------
inline float cpu_freq_mhz() { return 0.0f; }
inline float cpu_cores()    {
  unsigned n = std::thread::hardware_concurrency();
  return n > 0 ? (float)n : 1.0f;
}

// -- Temperature ------------------------------------------------------------
inline float core_temp() { return 0.0f; }

// -- Versions ---------------------------------------------------------------
inline const char* platform_version() { return "pc"; }
inline const char* sdk_version()      { return ""; }

// -- Flash chip details (ESP32-only meaningful; PC reports zero/empty) ------
inline float       flash_size_mb()   { return 0.0f; }
inline float       flash_speed_mhz() { return 0.0f; }
inline const char* flash_chip_mode() { return ""; }

}  // namespace pal
