// Behavioral tests for DataBuffer<RGB> / DataBufferReader — single-slot SPSC contract.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

#include <doctest/doctest.h>

#include "core/DataBuffer.h"
#include "modules/lights/RGB.h"

using namespace pmm;

TEST_CASE("DataBuffer: allocate(0) leaves the buffer invalid") {
  DataBuffer<RGB> b;
  CHECK(b.allocate(0));
  CHECK(!b.valid());
  DataBufferReader<RGB> r;
  r.attach(&b);
  CHECK(r.try_acquire_read() == nullptr);
}

TEST_CASE("DataBuffer: reader returns nullptr before first publish") {
  DataBuffer<RGB> b;
  REQUIRE(b.allocate(64));
  DataBufferReader<RGB> r;
  r.attach(&b);
  CHECK(r.try_acquire_read() == nullptr);
}

TEST_CASE("DataBuffer: publish then read returns the filled slot") {
  DataBuffer<RGB> b;
  REQUIRE(b.allocate(4));
  RGB* w = b.acquire_write();
  REQUIRE(w != nullptr);
  for (int i = 0; i < 4; ++i) w[i] = {(uint8_t)(i + 1), 0, 0};
  b.publish();

  DataBufferReader<RGB> r;
  r.attach(&b);
  const RGB* p = r.try_acquire_read();
  REQUIRE(p != nullptr);
  CHECK(p[0].r == 1);
  CHECK(p[3].r == 4);
  r.release_read();
}

TEST_CASE("DataBuffer: acquire_write returns the same slot every time") {
  DataBuffer<RGB> b; REQUIRE(b.allocate(1));
  RGB* a = b.acquire_write(); b.publish();
  RGB* c = b.acquire_write(); b.publish();
  CHECK(a == c);
}

TEST_CASE("DataBuffer: revision increments on each publish") {
  DataBuffer<RGB> b; REQUIRE(b.allocate(4));
  const uint32_t rev0 = b.revision();
  b.acquire_write()[0] = {1, 2, 3}; b.publish();
  const uint32_t rev1 = b.revision();
  CHECK(rev1 != rev0);
  b.publish(); CHECK(b.revision() != rev1);
}

TEST_CASE("DataBuffer: reader returns nullptr after release_read with no new publish") {
  DataBuffer<RGB> b;
  REQUIRE(b.allocate(4));
  b.acquire_write()[0] = {7, 7, 7};
  b.publish();
  DataBufferReader<RGB> r;
  r.attach(&b);
  REQUIRE(r.try_acquire_read() != nullptr);
  r.release_read();
  CHECK(r.try_acquire_read() == nullptr);
}

TEST_CASE("DataBuffer: two readers are independent — one release does not affect the other") {
  DataBuffer<RGB> b;
  REQUIRE(b.allocate(4));
  b.acquire_write()[0] = {42, 0, 0};
  b.publish();

  DataBufferReader<RGB> r1, r2;
  r1.attach(&b);
  r2.attach(&b);

  // Both see the frame.
  REQUIRE(r1.try_acquire_read() != nullptr);
  REQUIRE(r2.try_acquire_read() != nullptr);

  // r1 releases — r2 should still see the frame.
  r1.release_read();
  CHECK(r1.try_acquire_read() == nullptr);  // r1: nothing new
  CHECK(r2.try_acquire_read() != nullptr);  // r2: still has the frame
  r2.release_read();
  CHECK(r2.try_acquire_read() == nullptr);
}

TEST_CASE("DataBuffer: SPSC cross-core consumer never tears under paced producer") {
  constexpr uint32_t kCount = 64;
  constexpr int      kIters = 5'000;
  DataBuffer<RGB> b;
  REQUIRE(b.allocate(kCount));

  std::atomic<bool> stop{false};
  std::atomic<uint64_t> reads{0}, tears{0};

  std::thread consumer([&] {
    DataBufferReader<RGB> r; r.attach(&b);
    while (!stop.load(std::memory_order_relaxed)) {
      const RGB* slot = r.try_acquire_read();
      if (!slot) { std::this_thread::yield(); continue; }
      const uint8_t tag = slot[0].r;
      for (uint32_t i = 1; i < kCount; ++i)
        if (slot[i].r != tag || slot[i].g != tag || slot[i].b != tag)
          { tears.fetch_add(1, std::memory_order_relaxed); break; }
      reads.fetch_add(1, std::memory_order_relaxed);
      r.release_read();
    }
  });

  for (int n = 0; n < kIters; ++n) {
    RGB* w = b.acquire_write();
    uint8_t tag = (uint8_t)(n & 0xff);
    for (uint32_t i = 0; i < kCount; ++i) w[i] = {tag, tag, tag};
    b.publish();
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }
  stop.store(true, std::memory_order_relaxed);
  consumer.join();
  CHECK(tears.load() == 0);
  CHECK(reads.load() > 0);
}
