#pragma once
//
// DataBuffer<T> — single-slot shared pixel buffer with revision counter.
// DataBufferReader<T> — per-consumer read cursor into a DataBuffer.
//
// One producer owns a DataBuffer and writes into it each frame.
// Each consumer owns a DataBufferReader that tracks its own read position
// independently — multiple consumers do not interfere with each other.
//
// Hot-path contract:
//   buf.acquire_write()       — slot pointer; zero branches, zero alloc
//   buf.publish()             — one atomic store (release); bumps revision
//   reader.try_acquire_read() — two atomic loads; nullptr if no new frame
//   reader.release_read()     — one atomic store (release)
//
// Memory ordering:
//   publish()          — memory_order_release  (producer)
//   try_acquire_read() — memory_order_acquire  (consumer)
//   32-bit atomics are single-instruction on Xtensa LX6/LX7.
//
// T must be trivially copyable (RGB, uint8_t, etc.).
//

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "../pal/PalHeap.h"

namespace pmm {

template <typename T> class DataBufferReader;  // forward declaration for friend

template <typename T>
class DataBuffer {
 public:
  DataBuffer() = default;
  ~DataBuffer() { free_(); }
  DataBuffer(const DataBuffer&)            = delete;
  DataBuffer& operator=(const DataBuffer&) = delete;

  bool allocate(size_t count) {
    free_();
    if (count == 0) return true;
    slot_ = static_cast<T*>(pal::psram_alloc(count * sizeof(T)));
    if (!slot_) return false;
    count_ = count;
    published_.store(kNone, std::memory_order_relaxed);
    return true;
  }

  bool   valid()      const { return slot_ != nullptr; }
  size_t count()      const { return count_; }
  size_t slot_bytes() const { return count_ * sizeof(T); }

  // ── Producer side ────────────────────────────────────────────────────────

  T* acquire_write() { return slot_; }

  void publish() {
    published_.store(published_.load(std::memory_order_relaxed) + 1,
                     std::memory_order_release);
  }

  // Revision: monotonically increasing. Consumers use it to detect geometry
  // changes or torn frames (single-slot, no double-buffer).
  uint32_t revision() const {
    return published_.load(std::memory_order_acquire);
  }

  static constexpr uint32_t kNone = (uint32_t)-1;

 private:
  friend class DataBufferReader<T>;

  T*                    slot_      = nullptr;
  size_t                count_     = 0;
  std::atomic<uint32_t> published_ { kNone };

  void free_() {
    if (slot_) { pal::psram_free(slot_); slot_ = nullptr; }
    count_ = 0;
    published_.store(kNone, std::memory_order_relaxed);
  }
};

// ── Per-consumer read cursor ──────────────────────────────────────────────────
//
// Each consumer that reads from a DataBuffer<T> owns one DataBufferReader<T>.
// The reader holds its own consumed_ counter so multiple consumers are
// fully independent — one consumer calling release_read() does not affect
// any other consumer's view of "have I seen this frame yet?".
//
// Usage:
//   DataBufferReader<RGB> reader_;   // member of the consumer module
//   reader_.attach(buf);             // called from setup() / resolve_buf_()
//   const RGB* p = reader_.try_acquire_read();
//   if (p) { /* use p */ reader_.release_read(); }

template <typename T>
class DataBufferReader {
 public:
  void attach(DataBuffer<T>* buf) {
    buf_      = buf;
    consumed_.store(DataBuffer<T>::kNone, std::memory_order_relaxed);
  }

  void detach() { buf_ = nullptr; }

  const T* try_acquire_read() {
    if (!buf_) return nullptr;
    const uint32_t pub = buf_->published_.load(std::memory_order_acquire);
    if (pub == DataBuffer<T>::kNone) return nullptr;
    if (pub == consumed_.load(std::memory_order_relaxed)) return nullptr;
    return buf_->slot_;
  }

  void release_read() {
    if (!buf_) return;
    consumed_.store(buf_->published_.load(std::memory_order_relaxed),
                    std::memory_order_release);
  }

  // Convenience: slot_bytes / revision forwarded from the buffer.
  size_t   slot_bytes() const { return buf_ ? buf_->slot_bytes() : 0; }
  uint32_t revision()   const { return buf_ ? buf_->revision()   : DataBuffer<T>::kNone; }
  bool     valid()      const { return buf_ != nullptr && buf_->valid(); }

 private:
  DataBuffer<T>*        buf_      = nullptr;
  std::atomic<uint32_t> consumed_ { DataBuffer<T>::kNone };
};

}  // namespace pmm
