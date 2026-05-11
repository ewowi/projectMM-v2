#pragma once

#include <chrono>
#include <cstdint>
#include <thread>

namespace pal {

inline uint32_t millis() {
  using clock = std::chrono::steady_clock;
  static const auto t0 = clock::now();
  return static_cast<uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t0).count());
}

inline uint32_t micros() {
  using clock = std::chrono::steady_clock;
  static const auto t0 = clock::now();
  return static_cast<uint32_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(clock::now() - t0).count());
}

inline void sleep(uint32_t ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

inline void yield() { std::this_thread::yield(); }

}  // namespace pal
