// Cross-core SPSC tests for pmm::DataRing<RGB>.
// Producer thread fills a tagged pattern; consumer thread reads and asserts
// the most-recently-published slot is internally consistent. Verifies the
// release/acquire pairing that makes the cross-core path on Xtensa correct.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

#include <doctest/doctest.h>

#include "core/DataRing.h"
#include "modules/lights/RGB.h"

using namespace pmm;

TEST_CASE("DataRing: allocate(0) is a no-op that leaves the ring invalid") {
  DataRing<RGB> r;
  CHECK(r.allocate(0, 2));     // zero-count allocate succeeds
  CHECK(!r.valid());            // but no slots are owned
  CHECK(r.try_acquire_read() == nullptr);
}

TEST_CASE("DataRing: allocate then read returns nullptr before first publish") {
  DataRing<RGB> r;
  REQUIRE(r.allocate(64, 2));
  REQUIRE(r.valid());
  CHECK(r.try_acquire_read() == nullptr);  // producer hasn't published yet
}

TEST_CASE("DataRing: publish then read returns the just-filled slot") {
  DataRing<RGB> r;
  REQUIRE(r.allocate(4, 2));
  RGB* w = r.acquire_write_slot();
  REQUIRE(w != nullptr);
  for (int i = 0; i < 4; ++i) w[i] = {(uint8_t)(i + 1), 0, 0};
  r.publish();

  const RGB* read = r.try_acquire_read();
  REQUIRE(read != nullptr);
  CHECK(read[0].r == 1);
  CHECK(read[3].r == 4);
  r.release_read();
}

TEST_CASE("DataRing: slot rotation — successive publishes alternate slots") {
  DataRing<RGB> r;
  REQUIRE(r.allocate(1, 2));
  RGB* a = r.acquire_write_slot();
  r.publish();
  RGB* b = r.acquire_write_slot();
  r.publish();
  RGB* c = r.acquire_write_slot();
  CHECK(a != b);   // depth-2 → producer alternates
  CHECK(a == c);   // back to the first slot on the third write
}

TEST_CASE("DataRing: depth-1 revision increments on each publish") {
  DataRing<RGB> r;
  REQUIRE(r.allocate(4, 1));
  const uint32_t rev0 = r.revision();
  RGB* w = r.acquire_write_slot();
  w[0] = {1, 2, 3};
  r.publish();
  CHECK(r.revision() != rev0);
}

TEST_CASE("DataRing: SPSC cross-core consumer never tears under paced producer") {
  // Producer + consumer on two threads. Producer paces at ~10 kHz (100 us
  // per publish) — well below the rate at which it could lap the consumer.
  // Verifies the load-bearing invariant: release/acquire pairing delivers
  // consistent frames byte-by-byte.
  constexpr uint32_t kCount = 64;
  constexpr int      kIters = 5'000;
  DataRing<RGB> r;
  REQUIRE(r.allocate(kCount, 2));

  std::atomic<bool>     stop{false};
  std::atomic<uint64_t> reads{0};
  std::atomic<uint64_t> tears{0};

  std::thread consumer([&] {
    while (!stop.load(std::memory_order_relaxed)) {
      const RGB* slot = r.try_acquire_read();
      if (!slot) { std::this_thread::yield(); continue; }
      const uint8_t tag = slot[0].r;
      for (uint32_t i = 1; i < kCount; ++i) {
        if (slot[i].r != tag || slot[i].g != tag || slot[i].b != tag) {
          tears.fetch_add(1, std::memory_order_relaxed);
          break;
        }
      }
      reads.fetch_add(1, std::memory_order_relaxed);
      r.release_read();
    }
  });

  for (int n = 0; n < kIters; ++n) {
    RGB*    w   = r.acquire_write_slot();
    uint8_t tag = (uint8_t)(n & 0xff);
    for (uint32_t i = 0; i < kCount; ++i) w[i] = {tag, tag, tag};
    r.publish();
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  stop.store(true, std::memory_order_relaxed);
  consumer.join();

  CHECK(tears.load() == 0);
  CHECK(reads.load() > 0);
}
