# ArtnetOutModule

Packs the current pixel frame from a named source into Art-Net OpDmx packets and sends them via UDP. 510 DMX bytes (= 170 RGB pixels) per packet → multi-universe at large geometries (e.g. 128×128×1 = 49 152 RGB bytes = 97 universes per frame, ~6 Mbps at 50 fps). Runs on core 1 (set via constructor `setCoreAffinity(1)`); reads frames cross-core from the source's [`DataBuffer<RGB>`](../../developer-guide/backend.md#databuffert-the-shared-slot-primitive) via a single acquire/release pair — no per-frame copy on the consumer side.

**Wire format** (one Art-Net OpDmx packet per universe, sent to `dest_ip:6454`):

```
"Art-Net\0"          8 bytes  ID
0x00 0x50            2 bytes  OpCode (OpDmx, little-endian)
0x00 0x0e            2 bytes  ProtVer 14 (big-endian per spec)
seq                  1 byte   sequence (0 = ignore)
phys                 1 byte   physical port (0)
uni_lo uni_hi        2 bytes  universe (little-endian)
len_hi len_lo        2 bytes  DMX byte count (big-endian)
<data>               1..510 bytes  RGB triplets
```

## End-user reference

| Control | Type | Range / default | Notes |
|---|---|---|---|
| `enabled` | toggle | true | |
| `source` | text | `ripples-0` | Module id whose frame to send (resolved via `DataRegistry`) |
| `dest_ip` | text | `255.255.255.255` | Receiver IP (broadcast by default; set to a specific Art-Net node) |
| `universe` | range | 0..15 / 0 | Starting Art-Net universe number (subsequent packets increment) |

## Developer reference

- Constructor: `setCoreAffinity(1)` — runs on APP_CPU so the consumer doesn't share core 0 with the producer.
- `setup()` — `udp_.begin()` (bind ephemeral source port), `resolve_buf_()` (`DataRegistry` lookup by `source` id).
- `onUpdate(key)` — for `source` edits, re-call `resolve_buf_()`.
- `loop20ms()` — `buf_->try_acquire_read()` (cross-core SPSC, release/acquire ordering); if null (no new frame), return early. Pack one Art-Net OpDmx packet per 510-byte universe slice via `pack_header()`, send via `udp_.send()`, `buf_->release_read()`.
- `pack_header()` — public static helper exposed for [`test_artnet_packing.cpp`](https://github.com/ewowi/projectMM-v2/blob/main/test/test_pc/test_artnet_packing.cpp).
