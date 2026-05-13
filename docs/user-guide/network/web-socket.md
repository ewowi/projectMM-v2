# WebSocketModule

Pushes module schema + state to connected frontend clients, accepts inbound control updates, and forwards binary preview frames pushed by [PreviewModule](../lights/preview.md). Listens on port 81. Same WiFi-readiness pattern as [HttpServerModule](http-server.md) — `loop1s` self-starts the listener once a netif exists.

## End-user reference

No user-facing controls beyond `enabled`. The user-visible surface is the WS wire contract:

- **Schema frame** (text, JSON): `{"t":"schema","modules":[{id, type, controls:[{key, type, min, max, default}, …]}, …]}`. Broadcast once per client connect and again whenever any module sets `schemaDirty_` (added / removed / type changed).
- **State frame** (text, JSON): raw top-level array `[{id, controls:{key:value, …}, timing:{ms_per_tick, self_ms_per_tick}}, …]`. Broadcast at 1 Hz from `loop1s` when `hasClients()`.
- **Binary preview frame** (binary): forwarded byte-for-byte from PreviewModule's `broadcastBinary` call. Header + RGB body documented in [PreviewModule → wire format](../lights/preview.md).

## Developer reference

- `setup()` — construct `pal::WsServer` on port 81. Register the binary-frame relay so PreviewModule can push without knowing about clients directly.
- `loop20ms()` — ESP32 only: cleanup closed client slots from the underlying AsyncWebSocket. No-op on PC.
- `loop1s()` — call `server_->begin()` idempotently (WiFi-readiness pattern). If `hasClients()`: broadcast schema when `schemaDirty_` is set (after add / remove / type change), then broadcast state (per-control values + per-module timing).
- `broadcastBinary(data, len)` — public method used by PreviewModule to push binary frames; skipped if no clients are connected.
- `hasClients()` — public accessor used by PreviewModule + others to skip per-tick work when no one is listening.
