#pragma once
//
// PalWifi — WiFi station-mode primitives consumed by modules/system/WifiStaModule.
// ESP32: thin wrappers over arduino-esp32's WiFi class. PC: no-op stubs that
// always report "disconnected" so module logic stays platform-neutral.
//
// Scope is deliberately minimal for Sprint 5: connect, disconnect, poll
// connection state, read local IP + RSSI. AP mode, mDNS, multi-network
// fallback, and TX-power tuning land in later sprints when a caller needs
// them (per CLAUDE.md Rule #1 — each addition pays for itself).
//

#include <cstddef>
#include <cstring>

#ifdef ARDUINO
  #include <WiFi.h>
#endif

namespace pal {

inline void wifi_begin(const char* ssid, const char* password) {
#ifdef ARDUINO
  WiFi.enableSTA(true);
  WiFi.begin(ssid, (password && password[0]) ? password : nullptr);
#else
  (void)ssid;
  (void)password;
#endif
}

inline bool wifi_is_connected() {
#ifdef ARDUINO
  return WiFi.status() == WL_CONNECTED;
#else
  return false;
#endif
}

inline void wifi_disconnect() {
#ifdef ARDUINO
  WiFi.disconnect(/*wifioff=*/false);
#endif
}

inline void wifi_local_ip(char* buf, size_t len) {
  if (!buf || !len) return;
#ifdef ARDUINO
  std::strncpy(buf, WiFi.localIP().toString().c_str(), len - 1);
  buf[len - 1] = '\0';
#else
  buf[0] = '\0';
#endif
}

// RSSI in dBm. ESP32 returns 0 when disconnected; we return -127 (the wire
// value libraries use for "no signal") so callers can distinguish unambiguously.
inline int wifi_rssi() {
#ifdef ARDUINO
  return WiFi.isConnected() ? (int)WiFi.RSSI() : -127;
#else
  return -127;
#endif
}

// Current TX power in dBm. arduino-esp32's wifi_power_t enum values are
// quarter-dBm; divide by 4 to get the float. -1 (~MINUS_1dBm) is the
// driver's "unset" indicator before WiFi.begin runs. PC: returns 0.
inline float wifi_tx_power_dbm() {
#ifdef ARDUINO
  return (int)WiFi.getTxPower() / 4.0f;
#else
  return 0.0f;
#endif
}

// Set WiFi TX power. `dbm` is the target in dBm; arduino-esp32 accepts
// quarter-dBm steps via esp_wifi_set_max_tx_power and clamps to a small set
// of named values (roughly -1, 2, 5, 7, 8.5, 11, 13, 15, 17, 18.5, 19, 19.5).
// Useful for boards whose antenna / RF layout drops packets at the default
// ~19.5 dBm — WifiStaModule calls this only after a timed-out connect, so
// healthy boards keep their full power budget.
inline void wifi_set_tx_power(float dbm) {
#ifdef ARDUINO
  WiFi.setTxPower((wifi_power_t)(int)(dbm * 4));
#else
  (void)dbm;
#endif
}

}  // namespace pal
