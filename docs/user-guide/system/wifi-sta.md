# WifiStaModule

Connects the device to a WiFi network using credentials read from `/wifi.json` (LittleFS partition; see [Deploy → WiFi provisioning](../../developer-guide/deploy.md#wifi-provisioning)). Implements smart TX-power adaptation: starts at 19.5 dBm, steps down through 17 / 15 / 13 / 11 / 8.5 dBm on connect timeout (finds the *highest* working power rather than jumping to the floor), probes back up every hour. Editing `ssid` / `password` rewrites `/wifi.json` and reconnects.

## End-user reference

| Control | Type | Range / default | Notes |
|---|---|---|---|
| `enabled` | toggle | true | |
| `ssid` | text | — | Network SSID; edits trigger reconnect |
| `password` | password | — | WPA passphrase |
| `status` | display | — | `connected` / `connecting…` / `no credentials` / etc. |
| `ip` | display | — | Local IP once connected |
| `rssi_dbm` | display | -127..0 | Signal strength |
| `tx_power_dbm` | display | 0..21 | Current TX power (post-adaptation) |

## Developer reference

- `setup()` — load `/wifi.json` via `pal::fs_read_text`; call `pal::wifi_begin` if credentials exist.
- `loop1s()` — poll `pal::wifi_is_connected`; on disconnect, step TX power through `kTxSteps[]`. Hourly probe-back-up to detect improved conditions.
- `onUpdate(key)` — for `ssid` or `password` edits, rewrite `/wifi.json` and reconnect.
