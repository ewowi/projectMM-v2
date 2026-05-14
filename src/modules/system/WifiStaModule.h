#pragma once
//
// WifiStaModule — WiFi station-mode connection driven by /wifi.json.
//
// Boot flow:
//   1. setup() reads /wifi.json via PalFs (LittleFS on ESP32; ./state/ on PC).
//   2. If ssid is non-empty, calls pal::wifi_begin(ssid, password).
//   3. loop1s() polls pal::wifi_is_connected() and refreshes status / ip /
//      rssi display controls. No work in loop() — connect is event-driven
//      from the WiFi driver's side, we just watch.
//   4. When the user edits ssid/password in the UI, onUpdate() rewrites
//      /wifi.json and re-invokes wifi_begin so the change takes effect
//      without a reboot.
//
// HTTP and WS modules poll pal::wifi_is_connected() in their own loop1s and
// start their listeners once the netif is up — that flow lives in their
// respective modules; WifiStaModule does not call into them.
//
// PC build: pal::wifi_* are stubs that always report disconnected. Reading
// ./state/wifi.json still works, which is useful for cross-platform tests of
// the config-load path.
//

#include <ArduinoJson.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "../../core/MoonModule.h"
#include "../../pal/Pal.h"
#include "../../pal/PalFs.h"
#include "../../pal/PalWifi.h"
#include "Logger.h"

namespace pmm {

class WifiStaModule : public MoonModule {
 public:
  const char* category() const override { return "network"; }

  void setup() override {
    load_creds_();
    if (ssid_[0] && enabled_) start_connect_();
    else                       std::strncpy(status_, ssid_[0] ? "disabled" : "no credentials", sizeof(status_) - 1);
  }

  void onBuildControls() override {
    addControl(ssid_,            sizeof(ssid_),     "ssid",       "text");
    addControl(password_,        sizeof(password_), "password",   "password");
    addControl(status_,                             "status",     "display");
    addControl(ip_address_,                         "ip",         "display");
    addControl((int8_t)rssi_,                       "rssi_dbm",   "display", (int8_t)-127, (int8_t)0);
    addControl(tx_power_dbm_,                       "tx_power_dbm", "display", 0.0f, 21.0f);  // float: quarter-dBm steps
    // tx_power_pref_dbm: user-settable override. 0 = "auto" (default staircase
    // kicks in only on connect timeout). Any non-zero value forces this level
    // on every connect. Persisted in /wifi.json so it survives reboots.
    addControl(tx_power_pref_dbm_,                  "tx_power_pref_dbm", "slider", 0.0f, 21.0f);
  }

  void loop1s() override {
    // Periodic promotion probe: if we're connected at reduced power, try
    // default power once an hour to see if conditions have improved (RF
    // crowding cleared, neighbour's microwave turned off, etc.). Boards
    // with a genuinely weak antenna will disassociate within seconds and
    // we demote back; environments where reduction was transient stay
    // promoted for the rest of the session.
    if (!connecting_ && reduced_tx_power_dbm_() > 0.0f && pal::wifi_is_connected()) {
      maybe_run_promotion_probe_();
    }

    if (connecting_) {
      if (pal::wifi_is_connected()) {
        pal::wifi_local_ip(ip_address_, sizeof(ip_address_));
        rssi_         = (int8_t)pal::wifi_rssi();
        tx_power_dbm_ = pal::wifi_tx_power_dbm();
        std::strncpy(status_, "connected", sizeof(status_) - 1);
        connecting_ = false;
        if (reduced_tx_power_dbm_() > 0.0f) next_probe_ms_ = pal::millis() + kPromoteIntervalMs;
        log("[wifi] connected ssid=%s ip=%s rssi=%ddBm tx=%.1fdBm\n",
            ssid_, ip_address_, (int)rssi_, tx_power_dbm_);
      } else if (pal::millis() - connect_start_ms_ > CONNECT_TIMEOUT_MS) {
        // Connect timed out. Step down one rung on the TX-power staircase
        // and retry immediately — *don't* wait RETRY_INTERVAL_MS, the user
        // wants the device online ASAP. Once we exhaust the table (already
        // at the lowest level), give up until the slow-cadence retry below.
        if (tx_step_idx_ + 1 < (int)kTxStepsLen) {
          ++tx_step_idx_;
          log("[wifi] timeout — retrying at %.1f dBm (step %d/%d)\n",
              kTxSteps[tx_step_idx_], tx_step_idx_ + 1, (int)kTxStepsLen);
          start_connect_();
        } else {
          std::strncpy(status_, "failed", sizeof(status_) - 1);
          ip_address_[0] = '\0';
          rssi_ = -127;
          connecting_ = false;
          log("[wifi] connect timeout ssid=%s (low TX did not help)\n", ssid_);
        }
      }
      return;
    }
    if (!enabled_ || ssid_[0] == '\0') return;
    if (pal::wifi_is_connected()) {
      rssi_         = (float)pal::wifi_rssi();
      tx_power_dbm_ = pal::wifi_tx_power_dbm();
      return;
    }
    // Was connected, now isn't — auto-retry on a slow cadence so router
    // reboots / brief signal loss recover without manual action.
    if (pal::millis() - last_retry_ms_ >= RETRY_INTERVAL_MS) {
      log("[wifi] retrying ssid=%s\n", ssid_);
      start_connect_();
    }
  }

  void onUpdate(const char* key) override {
    if (std::strcmp(key, "enabled") == 0) {
      if (enabled_) start_connect_();
      else {
        pal::wifi_disconnect();
        connecting_ = false;
        ip_address_[0] = '\0';
        rssi_ = -127.0f;
        std::strncpy(status_, "disabled", sizeof(status_) - 1);
      }
      return;
    }
    if (std::strcmp(key, "ssid") == 0 || std::strcmp(key, "password") == 0) {
      save_creds_();
      if (enabled_) start_connect_();
      return;
    }
    if (std::strcmp(key, "tx_power_pref_dbm") == 0) {
      // Clamp + persist. 0 = back to auto. Apply live if connected — no
      // need to re-associate; arduino-esp32 accepts setTxPower while up.
      // Also clear the staircase + cancel any pending promotion probe so
      // the auto path stops fighting the user's choice.
      if (tx_power_pref_dbm_ < 0.0f || tx_power_pref_dbm_ > 21.0f) tx_power_pref_dbm_ = 0.0f;
      save_creds_();
      tx_step_idx_      = -1;
      next_probe_ms_    = 0;
      probe_started_ms_ = 0;
      if (pal::wifi_is_connected() && tx_power_pref_dbm_ > 0.0f) {
        pal::wifi_set_tx_power(tx_power_pref_dbm_);
        tx_power_dbm_ = pal::wifi_tx_power_dbm();
        log("[wifi] TX pref applied %.1f dBm (live)\n", tx_power_pref_dbm_);
      }
    }
  }

 private:
  char    ssid_[33]        = {};
  char    password_[65]    = {};
  char    status_[24]      = "boot";
  char    ip_address_[16]  = {};
  int8_t  rssi_            = -127;
  float   tx_power_dbm_    = 0.0f;
  // User override loaded from /wifi.json. 0 = auto (use the staircase only on
  // connect timeout); >0 = force this level on every connect, ignore the
  // staircase. Edited via the slider; persisted by save_creds_().
  float   tx_power_pref_dbm_ = 0.0f;
  bool    connecting_      = false;
  // Index into kTxSteps for the active reduced level, or -1 while we're
  // still at full default power. Each connect timeout advances by 1; once
  // we exhaust the table we stop stepping down. Per-boot only.
  int     tx_step_idx_     = -1;
  // Reduced TX power currently applied (= kTxSteps[tx_step_idx_]), or 0 at
  // default power. Cached so we don't index the table from hot paths.
  float   reduced_tx_power_dbm_() const {
    return tx_step_idx_ < 0 ? 0.0f : kTxSteps[tx_step_idx_];
  }
  uint32_t connect_start_ms_ = 0;
  uint32_t last_retry_ms_    = 0;
  // Promotion-probe state: while connected at reduced power, every
  // kPromoteIntervalMs we re-raise TX power to the default and watch for
  // kPromoteWatchMs. If the link survives, we declare conditions improved
  // and clear reduced_tx_power_dbm_; if it disassociates, we demote back.
  uint32_t next_probe_ms_       = 0;  // 0 = no probe pending
  uint32_t probe_started_ms_    = 0;  // 0 = not currently probing

  static constexpr const char* kCredsPath = "/wifi.json";
  static constexpr uint32_t CONNECT_TIMEOUT_MS = 15000;
  static constexpr uint32_t RETRY_INTERVAL_MS  = 30000;
  // TX-power staircase: tried in order on successive connect timeouts.
  // 19.5 dBm is the default (we start here, no override). 8.5 dBm is the
  // long-established Lolin workaround floor (v1 used exactly this). Levels
  // between let the algorithm find the *highest* working power instead of
  // crashing straight to the floor — useful for boards that need only a
  // small reduction (mild current droop on a marginal USB port etc.).
  // Order matters: descending.
  static constexpr float kTxSteps[] = {17.0f, 15.0f, 13.0f, 11.0f, 8.5f};
  static constexpr size_t kTxStepsLen = sizeof(kTxSteps) / sizeof(kTxSteps[0]);
  static constexpr float kDefaultTxPowerDbm = 19.5f;
  // Promotion-probe cadence. 1 hour is conservative — long enough that a
  // genuinely weak board isn't reset constantly, short enough that crowded
  // RF conditions clearing get picked up the same evening.
  static constexpr uint32_t kPromoteIntervalMs = 60u * 60u * 1000u;
  // After bumping power, watch for this long to see if the link survives.
  // arduino-esp32 disassociates within a few seconds when TX power is too
  // high for the antenna, so 30 s is comfortably above the worst case.
  static constexpr uint32_t kPromoteWatchMs    = 30u * 1000u;

  void load_creds_() {
    std::string body = pal::fs_read_text(kCredsPath);
    if (body.empty()) return;
    JsonDocument doc;
    if (deserializeJson(doc, body)) return;
    const char* ssid = doc["ssid"] | "";
    const char* pass = doc["password"] | "";
    std::strncpy(ssid_,     ssid, sizeof(ssid_)     - 1);
    std::strncpy(password_, pass, sizeof(password_) - 1);
    // Optional. Absent / 0 = auto-staircase; clamp to a sane range so a
    // typo can't shut the radio off entirely.
    const float pref = doc["tx_power_dbm"] | 0.0f;
    tx_power_pref_dbm_ = (pref < 0.0f || pref > 21.0f) ? 0.0f : pref;
  }

  void save_creds_() {
    JsonDocument doc;
    doc["ssid"]     = ssid_;
    doc["password"] = password_;
    if (tx_power_pref_dbm_ > 0.0f) doc["tx_power_dbm"] = tx_power_pref_dbm_;
    std::string body;
    serializeJson(doc, body);
    if (!pal::fs_write_text(kCredsPath, body)) {
      log("[wifi] WARN: failed to write %s\n", kCredsPath);
    }
  }

  // Probe whether conditions allow returning to default TX power.
  // Precondition: caller has verified we're connected at reduced power.
  void maybe_run_promotion_probe_() {
    const uint32_t now = pal::millis();
    if (probe_started_ms_ == 0) {
      // Not currently probing — start a probe iff the interval has elapsed.
      if (next_probe_ms_ == 0 || now < next_probe_ms_) return;
      log("[wifi] probing whether default TX power (%.1f dBm) holds\n", kDefaultTxPowerDbm);
      pal::wifi_set_tx_power(kDefaultTxPowerDbm);
      tx_power_dbm_      = pal::wifi_tx_power_dbm();
      probe_started_ms_  = now;
      return;
    }
    // Currently probing: did the link survive?
    if (!pal::wifi_is_connected()) {
      log("[wifi] promotion failed — link dropped, restoring %.1f dBm\n", reduced_tx_power_dbm_());
      pal::wifi_set_tx_power(reduced_tx_power_dbm_());
      tx_power_dbm_     = pal::wifi_tx_power_dbm();
      probe_started_ms_ = 0;
      next_probe_ms_    = now + kPromoteIntervalMs;
      return;
    }
    if (now - probe_started_ms_ >= kPromoteWatchMs) {
      log("[wifi] promotion succeeded — staying at default TX power\n");
      tx_step_idx_      = -1;
      probe_started_ms_ = 0;
      next_probe_ms_    = 0;  // no further probes; we're back at full power
    }
  }

  void start_connect_() {
    if (ssid_[0] == '\0') {
      std::strncpy(status_, "no credentials", sizeof(status_) - 1);
      return;
    }
    ip_address_[0] = '\0';
    rssi_ = -127.0f;
    // Pick the TX level to apply this attempt: user pref wins, else the
    // staircase rung (0 = default = no override).
    const float forced = tx_power_pref_dbm_ > 0.0f
                         ? tx_power_pref_dbm_
                         : reduced_tx_power_dbm_();
    log("[wifi] connecting ssid=%s%s\n", ssid_,
        forced > 0.0f ? (tx_power_pref_dbm_ > 0.0f ? " (TX pref)" : " (reduced TX power)") : "");
    pal::wifi_begin(ssid_, password_);
    // arduino-esp32 resets TX power on every WiFi.begin(), so re-apply
    // after begin() — not before.
    if (forced > 0.0f) pal::wifi_set_tx_power(forced);
    connecting_ = true;
    connect_start_ms_ = pal::millis();
    last_retry_ms_    = connect_start_ms_;
    std::strncpy(status_, "connecting", sizeof(status_) - 1);
  }
};

}  // namespace pmm
