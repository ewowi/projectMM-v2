#pragma once
//
// PalSystemInfo — platform queries for chip identity, heap, filesystem,
// flash, CPU, reset reason, build/SDK versions, and wall-clock time.
// Used by modules/system/SystemStatusModule.
//
// Sprint 4: ESP32 branch lights up via ESP-IDF + arduino-esp32 calls. The
// filesystem accessors stay at 0 on both branches — LittleFS mount and
// fs_total/used belong to Sprint 5 with PalFs.h. Heap, PSRAM, sketch size,
// flash chip, chip identity, reset reason, and version strings are real on
// ESP32 starting now; on PC they remain stubs (the SystemStatusModule
// schema is the contract — its fields exist on both platforms, values
// degrade gracefully).
//

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

#ifdef ARDUINO
  #include <Arduino.h>
  #include <esp_chip_info.h>
  #include <esp_heap_caps.h>
  #include <esp_image_format.h>
  #include <esp_mac.h>
  #include <esp_ota_ops.h>
  #include <esp_system.h>
  #include <freertos/FreeRTOS.h>
  #include <freertos/task.h>
#else
  #include <thread>
#endif

#define PAL_XSTR(x) PAL_STR(x)
#define PAL_STR(x) #x

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
#ifdef ARDUINO
  esp_chip_info_t chip;
  esp_chip_info(&chip);
  std::snprintf(buf, len, "%s Rev %d", ESP.getChipModel(), chip.revision);
#else
  std::strncpy(buf, "pc", len - 1);
  buf[len - 1] = '\0';
#endif
}
inline void mac_address(char* buf, size_t len) {
  if (!buf || !len) return;
#ifdef ARDUINO
  uint8_t mac[6] = {};
  esp_efuse_mac_get_default(mac);
  std::snprintf(buf, len, "%02X:%02X:%02X:%02X:%02X:%02X",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
#else
  buf[0] = '\0';
#endif
}

// -- Reset reason -----------------------------------------------------------
inline const char* reset_reason_str() {
#ifdef ARDUINO
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "power-on";
    case ESP_RST_EXT:       return "ext-pin";
    case ESP_RST_SW:        return "software";
    case ESP_RST_PANIC:     return "panic";
    case ESP_RST_INT_WDT:   return "int-watchdog";
    case ESP_RST_TASK_WDT:  return "task-watchdog";
    case ESP_RST_WDT:       return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep-sleep";
    case ESP_RST_BROWNOUT:  return "brownout";
    case ESP_RST_SDIO:      return "sdio";
    default:                return "unknown";
  }
#else
  return "boot";
#endif
}
inline bool is_crash_reset() {
#ifdef ARDUINO
  esp_reset_reason_t r = esp_reset_reason();
  return r == ESP_RST_PANIC || r == ESP_RST_INT_WDT ||
         r == ESP_RST_TASK_WDT || r == ESP_RST_WDT;
#else
  return false;
#endif
}

// -- Heap -------------------------------------------------------------------
inline float total_heap_kb() {
#ifdef ARDUINO
  return heap_caps_get_total_size(MALLOC_CAP_INTERNAL) / 1024.0f;
#else
  return 0.0f;
#endif
}
inline float free_heap_kb() {
#ifdef ARDUINO
  return heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024.0f;
#else
  return 0.0f;
#endif
}
inline float max_alloc_kb() {
#ifdef ARDUINO
  return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024.0f;
#else
  return 0.0f;
#endif
}
inline float heap_min_kb() {
#ifdef ARDUINO
  return heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024.0f;
#else
  return 0.0f;
#endif
}

// -- PSRAM ------------------------------------------------------------------
inline float total_psram_kb() {
#ifdef ARDUINO
  return heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024.0f;
#else
  return 0.0f;
#endif
}
inline float free_psram_kb() {
#ifdef ARDUINO
  return heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024.0f;
#else
  return 0.0f;
#endif
}
inline const char* psram_mode() {
#ifdef ARDUINO
  return heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0 ? "spiram" : "none";
#else
  return "none";
#endif
}

// -- Filesystem -------------------------------------------------------------
// Both branches return zero. LittleFS access lands in Sprint 5 with PalFs.h.
inline float fs_total_kb() { return 0.0f; }
inline float fs_used_kb()  { return 0.0f; }

// -- Sketch / firmware size -------------------------------------------------
inline float sketch_kb() {
#ifdef ARDUINO
  uint32_t sz = ESP.getSketchSize();
  if (sz > 0) return sz / 1024.0f;
  const esp_partition_t* p = esp_ota_get_running_partition();
  if (!p) return 0.0f;
  esp_image_metadata_t meta = {};
  const esp_partition_pos_t pos = {p->address, p->size};
  return (esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &pos, &meta) == ESP_OK)
             ? meta.image_len / 1024.0f
             : 0.0f;
#else
  return 0.0f;
#endif
}
inline float sketch_partition_kb() {
#ifdef ARDUINO
  const esp_partition_t* p = esp_ota_get_running_partition();
  return p ? p->size / 1024.0f : 0.0f;
#else
  return 0.0f;
#endif
}

// -- CPU --------------------------------------------------------------------
inline float cpu_freq_mhz() {
#ifdef ARDUINO
  return (float)ESP.getCpuFreqMHz();
#else
  return 0.0f;
#endif
}
inline float cpu_cores() {
#ifdef ARDUINO
  return (float)portNUM_PROCESSORS;
#else
  unsigned n = std::thread::hardware_concurrency();
  return n > 0 ? (float)n : 1.0f;
#endif
}

// -- Temperature ------------------------------------------------------------
inline float core_temp() {
#ifdef ARDUINO
  return temperatureRead();
#else
  return 0.0f;
#endif
}

// -- Versions ---------------------------------------------------------------
inline const char* platform_version() {
#ifdef ARDUINO
  return "arduino-esp32 "
         PAL_XSTR(ESP_ARDUINO_VERSION_MAJOR) "."
         PAL_XSTR(ESP_ARDUINO_VERSION_MINOR) "."
         PAL_XSTR(ESP_ARDUINO_VERSION_PATCH);
#else
  return "pc";
#endif
}
inline const char* sdk_version() {
#ifdef ARDUINO
  return ESP.getSdkVersion();
#else
  return "";
#endif
}

// -- Flash chip details -----------------------------------------------------
inline float flash_size_mb() {
#ifdef ARDUINO
  return ESP.getFlashChipSize() / (1024.0f * 1024.0f);
#else
  return 0.0f;
#endif
}
inline float flash_speed_mhz() {
#ifdef ARDUINO
  return ESP.getFlashChipSpeed() / 1000000.0f;
#else
  return 0.0f;
#endif
}
inline const char* flash_chip_mode() {
#ifdef ARDUINO
  switch (ESP.getFlashChipMode()) {
    case FM_QIO:  return "QIO";
    case FM_QOUT: return "QOUT";
    case FM_DIO:  return "DIO";
    case FM_DOUT: return "DOUT";
    default:      return "unknown";
  }
#else
  return "";
#endif
}

}  // namespace pal
