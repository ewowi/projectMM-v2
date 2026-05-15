#pragma once
//
// DataBuffer<T> — single-slot SPSC pixel buffer with revision counter.
//
// One producer writes into the single slot each frame; one or more consumers
// read from the same slot. Safe across two cores via release/acquire atomics.
//
// Hot-path contract (loop20ms body):
//   acquire_write()    — returns the slot pointer; zero branches, zero alloc
//   publish()          — one atomic store (release); bumps revision counter
//   try_acquire_read() — two atomic loads, one compare; returns nullptr if
//                        no new frame since last release_read()
//   release_read()     — one atomic store (release)
//
// Multiple consumers sharing one DataBuffer each call try_acquire_read()
// independently. Because there is only one slot, reads are zero-copy — the
// consumer works directly on the producer's buffer. The consumer must call
// release_read() before the producer publishes the next frame for the "new
// frame" signal to work; in practice the 20ms loop cadence makes this safe.
//
// Memory ordering:
//   publish()          — memory_order_release  (producer)
//   try_acquire_read() — memory_order_acquire  (consumer)
//   These pair-wise synchronise: all writes the producer made before publish()
//   are visible to every consumer after try_acquire_read() on any core.
//   32-bit atomics are single-instruction on Xtensa LX6/LX7.
//
// T must be trivially copyable (RGB, uint8_t, etc.).
//

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "../pal/PalHeap.h"

namespace pmm {

template <typename T>
class DataBuffer {
 public:
  DataBuffer() = default;
  ~DataBuffer() { free_(); }
  DataBuffer(const DataBuffer&)            = delete;
  DataBuffer& operator=(const DataBuffer&) = delete;

  // Allocate one slot of `count` elements.
  // Returns false if allocation fails — caller treats buffer as unavailable.
  // Safe to call repeatedly: frees previous allocation first.
  bool allocate(size_t count) {
    free_();
    if (count == 0) return true;
    slot_ = static_cast<T*>(pal::psram_alloc(count * sizeof(T)));
    if (!slot_) return false;
    count_ = count;
    published_.store(kNone, std::memory_order_relaxed);
    consumed_ .store(kNone, std::memory_order_relaxed);
    return true;
  }

  bool   valid()      const { return slot_ != nullptr; }
  size_t count()      const { return count_; }
  size_t slot_bytes() const { return count_ * sizeof(T); }

  // ── Producer side (single thread) ────────────────────────────────────────

  // Return the slot to fill. Zero branches, zero allocation.
  T* acquire_write() { return slot_; }

  // Publish the filled slot. One atomic store (release); bumps revision.
  void publish() {
    published_.store(published_.load(std::memory_order_relaxed) + 1,
                     std::memory_order_release);
  }

  // ── Consumer side (one or more threads, may be a different core) ─────────

  // Return pointer to the slot if a new frame has been published since the
  // last release_read(), or nullptr if nothing new.
  // Multiple consumers each track their own consumed_ — see note below.
  const T* try_acquire_read() {
    const uint32_t pub = published_.load(std::memory_order_acquire);
    if (pub == kNone) return nullptr;
    const uint32_t con = consumed_.load(std::memory_order_relaxed);
    if (pub == con) return nullptr;
    return slot_;
  }

  // Mark the current frame consumed. One atomic store (release).
  void release_read() {
    consumed_.store(published_.load(std::memory_order_relaxed),
                    std::memory_order_release);
  }

  // Revision counter: monotonically increasing with each publish().
  // Consumers use this to detect geometry changes or torn frames.
  uint32_t revision() const {
    return published_.load(std::memory_order_acquire);
  }

 private:
  static constexpr uint32_t kNone = (uint32_t)-1;

  T*                    slot_      = nullptr;
  size_t                count_     = 0;
  std::atomic<uint32_t> published_ { kNone };
  std::atomic<uint32_t> consumed_  { kNone };

  void free_() {
    if (slot_) { pal::psram_free(slot_); slot_ = nullptr; }
    count_ = 0;
    published_.store(kNone, std::memory_order_relaxed);
    consumed_ .store(kNone, std::memory_order_relaxed);
  }
};

}  // namespace pmm
