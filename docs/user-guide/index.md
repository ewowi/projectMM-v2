# User Guide

Per-module reference for projectMM v2, grouped by the directory each module lives in under `src/modules/`. Every page is structured the same way:

- **End-user reference** — what the module does and what controls it exposes in the UI. Read this first when configuring a device.
- **Developer reference** — which [`MoonModule`](../architecture/system.md#moonmodule-the-contract) lifecycle methods the module overrides and what each implementation does. Read this when extending the module or writing a new one. `onBuildControls()` is intentionally omitted from the developer half — its job is to register the controls already documented in the end-user half.

## Modules

### System
[`src/modules/system/`](https://github.com/ewowi/projectMM-v2/tree/main/src/modules/system) — always-present infrastructure modules. The first four in main.cpp's boot list (`system`, `wifi-sta`, `http`, `ws`) plus `state-store`.

- [SystemStatusModule](system/system-status.md) — chip / heap / PSRAM / fs / build identity exposed as read-only controls + `/api/system` JSON.
- [WifiStaModule](system/wifi-sta.md) — WiFi-STA with smart TX-power adaptation.
- [StateStoreModule](system/state-store.md) — persists module list + per-control values across reboots (LittleFS).

### Network
[`src/modules/network/`](https://github.com/ewowi/projectMM-v2/tree/main/src/modules/network) — HTTP + WebSocket transports for the frontend.

- [HttpServerModule](network/http-server.md) — serves the SPA + the REST API.
- [WebSocketModule](network/web-socket.md) — pushes schema + state to clients at 1 Hz, accepts binary preview frames.

### Lights
[`src/modules/lights/`](https://github.com/ewowi/projectMM-v2/tree/main/src/modules/lights) — the first feature domain on top of the runtime.

- [RipplesEffect](lights/ripples-effect.md) — radial sine ripples on a 2D RGB panel.
- [PreviewModule](lights/preview.md) — ships the current frame to the frontend as a binary WS message.
- [ArtnetOutModule](lights/artnet-out.md) — packs the current frame into Art-Net OpDmx UDP packets.
