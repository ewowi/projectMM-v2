# PreviewModule

Pushes the current pixel frame from a named source to the frontend over WebSocket at 50 fps. Reads the source's [`DataBuffer<RGB>`](../../developer-guide/backend.md#databuffert-the-shared-slot-primitive) via `DataRegistry`. Skips the memcpy when no clients are connected.

**Wire format** (binary WS frame):

```
byte  0:    0x02         magic / version
bytes 1-2:  uint16 LE    width
bytes 3-4:  uint16 LE    height
bytes 5-6:  uint16 LE    depth
bytes 7..:  RGB[count]   width * height * depth, byte-packed
```

The 7-byte header is fixed; the body grows with the buffer geometry (768 B at 16×16×1, 49 152 B at 128×128×1). Matches v1's `renderPixelFrame` in `app.js` so the v1 frontend renders v2 frames unchanged.

## End-user reference

| Control | Type | Range / default | Notes |
|---|---|---|---|
| `enabled` | toggle | true | |
| `source` | text | `ripples-0` | Module id whose frame to broadcast |

## Developer reference

- `setup()` — call `resolve_buf_()` (`DataRegistry` lookup by `source` control value) and `resolve_ws_()` (manager lookup of `ws-0`).
- `onUpdate(key)` — for `source` edits, re-call `resolve_buf_()`.
- `loop20ms()` — if `ws_->hasClients()` is false, skip. Otherwise `buf_->try_acquire_read()`; check torn-frame via revision before/after; if ok, resize `frame_` if geometry changed, pack header + RGB body via `pack_frame()`, call `ws_->broadcastBinary()`, `buf_->release_read()`. Tolerates late-add producers by re-calling `resolve_buf_()` if the buffer pointer is still null.
- `teardown()` — drop buffer + ws pointers.
- `pack_frame()` / `required_frame_bytes()` — public static helpers exposed for [`test_preview_wire.cpp`](https://github.com/ewowi/projectMM-v2/blob/main/test/test_pc/test_preview_wire.cpp).
