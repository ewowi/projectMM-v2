#pragma once
//
// FrameRing — depth-2 single-producer / single-consumer ring of RGB
// buffers. Producer (effect, core 0) writes one slot per tick; consumer
// (Art-Net out, core 1) reads the most-recently-published slot. Slots
// allocate from PSRAM when available so 128×128×1 (49 152 B per slot,
// ~96 KB total) fits comfortably on esp32s3_n16r8 without touching
// internal heap; on esp32dev without PSRAM, the same call falls back to
// internal heap and may return null at large sizes — caller treats that
// as "no ring, no cross-core path" and the consumer just stays idle.
//
// Why two slots is enough: the producer ticks at 50 Hz; the consumer
// ticks at the same rate on a different core. At any instant the
// producer is filling one slot and the consumer is reading the other —
// no head-of-line blocking, no copies inside the loop20ms body. If the
// consumer is more than one tick behind, the producer overwrites
// (best-effort, no back-pressure) so the latest frame wins. Real-time
// pixel output values "fresh frame" over "every frame".
//
// Memory ordering: producer's `publish()` is `memory_order_release`,
// consumer's `try_acquire_read()` is `memory_order_acquire` — together
// they pair-wise synchronise so that the pixel writes the producer made
// before `publish` are visible to the consumer after `try_acquire_read`
// on the other core. The 32-bit atomics are single-instruction load/
// store on both Xtensa LX6 (esp32dev) and Xtensa LX7 (esp32s3) so the
// reads/writes are torn-free.
//

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "../../pal/PalHeap.h"
#include "RGB.h"

namespace pmm {

class FrameRing {
 public:
  // Allocate the two slots. Returns false if PSRAM (and the internal
  // fallback) couldn't satisfy `slot_bytes` — caller treats this as the
  // ring not being available and reads `nullptr` from the consumer side.
  bool allocate(size_t slot_bytes) {
    free_();
    slot_bytes_ = slot_bytes;
    if (slot_bytes == 0) return true;
    slots_[0] = (RGB*)pal::psram_alloc(slot_bytes);
    slots_[1] = (RGB*)pal::psram_alloc(slot_bytes);
    if (!slots_[0] || !slots_[1]) { free_(); return false; }
    write_idx_ = 0;
    published_.store((uint32_t)-1, std::memory_order_relaxed);  // nothing published yet
    consumed_.store((uint32_t)-1, std::memory_order_relaxed);
    return true;
  }

  ~FrameRing() { free_(); }

  // -- Producer side (single thread) --
  // Return the slot the producer should fill. Always returns a valid
  // pointer once allocate() succeeded; ring is implicit double-buffer so
  // there's no "full" state to wait for.
  RGB* acquire_write_slot() {
    return slots_[write_idx_];
  }
  size_t slot_bytes() const { return slot_bytes_; }

  // Publish the just-written slot. Increments the producer counter with
  // release ordering. The consumer's next try_acquire_read picks this up.
  void publish() {
    const uint32_t next = published_.load(std::memory_order_relaxed) + 1;
    published_.store(next, std::memory_order_release);
    write_idx_ ^= 1;  // flip to the other slot for the next fill
  }

  // -- Consumer side (single thread, may be a different core) --
  // Return a pointer to the most-recently-published slot, or nullptr if
  // nothing new has been published since the consumer's last release.
  const RGB* try_acquire_read() {
    const uint32_t pub = published_.load(std::memory_order_acquire);
    const uint32_t con = consumed_.load(std::memory_order_relaxed);
    if (pub == (uint32_t)-1 || pub == con) return nullptr;
    // Read the slot that was just filled — the OTHER one from write_idx_,
    // which the producer has already flipped. Use the counter parity.
    return slots_[pub & 1];
  }

  // Mark the slot as consumed. Doesn't free anything; just lets the
  // producer's next publish overwrite freely (no semantic effect with
  // depth-2 + always-overwrite, but keeps the API explicit for the
  // multi-slot generalisation later).
  void release_read() {
    consumed_.store(published_.load(std::memory_order_relaxed), std::memory_order_release);
  }

  bool valid() const { return slots_[0] != nullptr && slots_[1] != nullptr; }

 private:
  RGB*                  slots_[2]    = { nullptr, nullptr };
  size_t                slot_bytes_  = 0;
  uint8_t               write_idx_   = 0;          // producer-local
  std::atomic<uint32_t> published_   { (uint32_t)-1 };
  std::atomic<uint32_t> consumed_    { (uint32_t)-1 };

  void free_() {
    if (slots_[0]) pal::psram_free(slots_[0]);
    if (slots_[1]) pal::psram_free(slots_[1]);
    slots_[0] = slots_[1] = nullptr;
    slot_bytes_ = 0;
  }
};

}  // namespace pmm
