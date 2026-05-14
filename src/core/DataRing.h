#pragma once
//
// DataRing<T> — depth-configurable single-producer / single-consumer ring.
//
// Hot-path contract (loop20ms body):
//   acquire_write_slot() — one array index, zero branches
//   publish()            — one atomic store (release), one conditional XOR
//   try_acquire_read()   — two atomic loads, one compare, zero allocations
//   release_read()       — one atomic store (release)
//
// All allocation is in allocate(); the hot path never touches the heap.
//
// Depth semantics:
//   depth=1  Single slot. Producer writes in-place; consumer reads the same
//            slot with acquire/release ordering. Safe on a single core.
//            Torn-frame detection: consumer compares revision before/after
//            memcpy — if it changed, the frame is skipped (best-effort).
//            Zero copy overhead vs. depth=2 because write_idx_ never flips.
//   depth=2  Two slots. Standard double-buffer: producer fills one while
//            consumer reads the other. Safe across two cores. Preferred when
//            PSRAM is available (the extra slot goes to PSRAM).
//   depth>2  Reserved for future use (e.g. triple-buffer for very uneven
//            producer/consumer cadences). Not needed for Sprint 13.
//
// Memory ordering:
//   publish()          — memory_order_release  (producer)
//   try_acquire_read() — memory_order_acquire  (consumer)
//   Together these pair-wise synchronise: all writes the producer made before
//   publish() are visible to the consumer after try_acquire_read() on any
//   core. 32-bit atomics are single-instruction on Xtensa LX6/LX7.
//
// T must be trivially copyable (RGB, uint8_t, etc.).
//

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "../pal/PalHeap.h"

namespace pmm {

template <typename T>
class DataRing {
 public:
  DataRing() = default;
  ~DataRing() { free_(); }
  DataRing(const DataRing&)            = delete;
  DataRing& operator=(const DataRing&) = delete;

  // Allocate `depth` slots of `count` elements each.
  // Returns false if any allocation fails — caller treats ring as unavailable.
  // Safe to call repeatedly: frees previous allocation first.
  bool allocate(size_t count, uint8_t depth) {
    free_();
    if (count == 0 || depth == 0) return true;
    depth_ = depth > kMaxDepth ? kMaxDepth : depth;
    count_ = count;
    for (uint8_t i = 0; i < depth_; ++i) {
      slots_[i] = static_cast<T*>(pal::psram_alloc(count * sizeof(T)));
      if (!slots_[i]) { free_(); return false; }
    }
    write_idx_ = 0;
    published_.store(kNone, std::memory_order_relaxed);
    consumed_ .store(kNone, std::memory_order_relaxed);
    return true;
  }

  bool valid()  const { return slots_[0] != nullptr; }
  size_t count() const { return count_; }
  uint8_t depth() const { return depth_; }
  size_t slot_bytes() const { return count_ * sizeof(T); }

  // ── Producer side (single thread) ────────────────────────────────────────

  // Return the slot to fill. One array lookup, zero branches.
  T* acquire_write_slot() { return slots_[write_idx_]; }

  // Publish the filled slot. One atomic store (release).
  // At depth=1, write_idx_ stays 0 — the XOR is optimised away when
  // depth_ == 1 is a compile-time constant, but we keep it correct for
  // runtime depth selection.
  void publish() {
    const uint32_t next = published_.load(std::memory_order_relaxed) + 1;
    published_.store(next, std::memory_order_release);
    if (depth_ > 1) write_idx_ = (write_idx_ + 1) % depth_;
  }

  // ── Consumer side (single thread, may be a different core) ───────────────

  // Return pointer to the most-recently published slot, or nullptr if nothing
  // new since the last release_read(). Two atomic loads, one compare.
  // At depth=1: slot index is always 0. At depth=2: parity of published counter.
  const T* try_acquire_read() {
    const uint32_t pub = published_.load(std::memory_order_acquire);
    if (pub == kNone) return nullptr;
    if (depth_ == 1) return slots_[0];          // depth-1: always slot 0
    const uint32_t con = consumed_.load(std::memory_order_relaxed);
    if (pub == con) return nullptr;             // nothing new
    return slots_[pub % depth_];
  }

  // Mark consumed. One atomic store (release).
  void release_read() {
    consumed_.store(published_.load(std::memory_order_relaxed),
                    std::memory_order_release);
  }

  // Depth-1 torn-frame detection: read revision before and after a memcpy.
  // If they differ the producer wrote concurrently — skip the frame.
  uint32_t revision() const {
    return published_.load(std::memory_order_acquire);
  }

 private:
  static constexpr uint8_t  kMaxDepth = 4;
  static constexpr uint32_t kNone     = (uint32_t)-1;

  T*                    slots_[kMaxDepth] = {};
  size_t                count_      = 0;
  uint8_t               depth_      = 0;
  uint8_t               write_idx_  = 0;    // producer-local, never read by consumer
  std::atomic<uint32_t> published_  { kNone };
  std::atomic<uint32_t> consumed_   { kNone };

  void free_() {
    for (uint8_t i = 0; i < kMaxDepth; ++i) {
      if (slots_[i]) { pal::psram_free(slots_[i]); slots_[i] = nullptr; }
    }
    count_ = 0; depth_ = 0; write_idx_ = 0;
    published_.store(kNone, std::memory_order_relaxed);
    consumed_ .store(kNone, std::memory_order_relaxed);
  }
};

}  // namespace pmm
