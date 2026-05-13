# HttpServerModule

Serves the frontend (gzipped SPA bundle at `/`) and a JSON REST API. Default port 80 on ESP32, 8080 on PC (via `pal::default_http_port()`). Listener self-starts via `loop1s` once `WifiStaModule` brings up a netif — earlier attempts to start in `setup()` crashed because `AsyncServer::begin()` asserts on `xQueueSemaphoreTake` before lwIP is ready.

## End-user reference

No user-facing controls beyond `enabled`. The user-visible surface is the route table:

| Route | Method | Purpose |
|---|---|---|
| `/` | GET | Frontend SPA (gzipped bundle) |
| `/api/types` | GET | Registered module type names (for the frontend's "Add module" picker) |
| `/api/modules` | GET / POST / DELETE / PATCH | List modules; add / remove / patch one |
| `/api/modules/reorder` | POST | Reorder root-level modules |
| `/api/control` | POST | Single-control update (v1 wire format: `{id, key, value}`) |
| `/api/system` | GET | Aggregated [SystemStatusModule](../system/system-status.md) output |
| `/api/log` | GET | Last 64 lines from `pmm::Logger`'s ring |

## Developer reference

- `setup()` — construct `pal::HttpServer` on the configured port, register every REST route (`/`, `/api/*`). Route handlers acquire `manager_->mutex()` before mutating module state. The listener is **not** started here.
- `loop1s()` — call `server_->begin()` idempotently. The first call after WiFi brings up a netif actually starts the listener.
- `~HttpServerModule()` — `pal::HttpServer`'s destructor stops the listener and joins its worker thread (PC side; ESP32 just unregisters the AsyncWebServer).
