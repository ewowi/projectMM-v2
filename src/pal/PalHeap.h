#pragma once
//
// PalHeap — large-buffer allocation that prefers PSRAM when available.
// ESP32 with PSRAM (esp32s3_n16r8): PSRAM via `MALLOC_CAP_SPIRAM`, keeping
// the internal heap free for stacks and bookkeeping.
// ESP32 without PSRAM (classic esp32dev): falls back to DRAM so the same
// code path works transparently — callers see a smaller maximum allocation
// size, not a compile error. Both paths pin `MALLOC_CAP_8BIT` to guarantee
// byte-addressable memory; see the inline comment for the IRAM fault this
// avoids.
// PC: plain `malloc` / `free`.
//
// Sprint 7's first caller is RipplesEffect's pixel buffer + the FrameRing's
// two slots; at 128×128 each slot is 49 152 B and three slots together
// touch 144 KB — well above esp32dev's ~180 KB internal-heap budget, so
// `psram_alloc` returning nullptr is an expected failure mode there.
// Callers handle nullptr as "skip this buffer; emit no frame" rather than
// crashing.
//

#include <cstddef>

#ifdef ARDUINO

  #include <esp_heap_caps.h>

namespace pal {

inline void* psram_alloc(size_t bytes) {
  if (bytes == 0) return nullptr;
  // MALLOC_CAP_8BIT is critical: without it, MALLOC_CAP_INTERNAL on classic
  // ESP32 can hand back an IRAM-region buffer (0x40090000-ish) that only
  // supports 32-bit-aligned word accesses. A byte store like `pixels[i].r =`
  // hits the IRAM and the CPU faults with LoadStoreError. MALLOC_CAP_8BIT
  // restricts the choice to DRAM, where byte-wide access is fine.
  // Try PSRAM first (s3 has it); fall back to DRAM when absent/depleted.
  void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (p) return p;
  return heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

inline void psram_free(void* ptr) {
  if (ptr) heap_caps_free(ptr);
}

}  // namespace pal

#else  // PC

  #include <cstdlib>

namespace pal {

inline void* psram_alloc(size_t bytes) {
  return bytes == 0 ? nullptr : std::malloc(bytes);
}
inline void psram_free(void* ptr) { std::free(ptr); }

}  // namespace pal

#endif
