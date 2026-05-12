// Behavioral tests for pal::psram_alloc / pal::psram_free.
// On PC the implementation is plain malloc/free (PalHeap.h's #else branch);
// on ESP32 it's heap_caps_malloc with MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
// preferred, falling back to MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT. The
// tests below assert the surface contract that both branches must honour:
// zero-byte requests return null, non-zero requests return a writable
// pointer, and free(nullptr) is a safe no-op.

#include <cstdint>
#include <cstring>

#include <doctest/doctest.h>

#include "pal/PalHeap.h"

TEST_CASE("PalHeap: psram_alloc(0) returns null without allocating") {
  void* p = pal::psram_alloc(0);
  CHECK(p == nullptr);
  // Free on null is still a defined no-op (asserted in the next test).
}

TEST_CASE("PalHeap: psram_free(nullptr) is a no-op") {
  // No assertion beyond "does not crash"; matches the precondition the
  // FrameRing destructor and RipplesEffect::free_pixels_() both rely on.
  pal::psram_free(nullptr);
}

TEST_CASE("PalHeap: psram_alloc returns a writable, byte-addressable buffer") {
  constexpr size_t kBytes = 1024;
  uint8_t* p = (uint8_t*)pal::psram_alloc(kBytes);
  REQUIRE(p != nullptr);
  // Write each byte (this is the MALLOC_CAP_8BIT contract on ESP32: the
  // returned region must support byte-wide stores, not just word stores).
  for (size_t i = 0; i < kBytes; ++i) p[i] = (uint8_t)(i & 0xff);
  for (size_t i = 0; i < kBytes; ++i) CHECK(p[i] == (uint8_t)(i & 0xff));
  pal::psram_free(p);
}

TEST_CASE("PalHeap: paired alloc/free does not leak across many cycles") {
  // 1000 alloc/free cycles at 4 KB. On PC this passes trivially via the
  // libc allocator's reuse. On hardware it surfaces gradual fragmentation
  // or leaks if either branch in PalHeap.h is broken.
  for (int i = 0; i < 1000; ++i) {
    void* p = pal::psram_alloc(4096);
    REQUIRE(p != nullptr);
    pal::psram_free(p);
  }
}
