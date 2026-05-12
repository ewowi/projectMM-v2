#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>

#ifdef ARDUINO
  #include <Arduino.h>
#else
  #include <csignal>
#endif

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

// Bring up the platform's stdio channel. PC: stdout works out of the box,
// just disable line buffering so pipes flush immediately. ESP32: open the
// USB / UART Serial port at the given baud and pause briefly so the host
// terminal can attach before the first byte ships.
inline void log_init(uint32_t baud = 115200) {
#ifdef ARDUINO
  Serial.begin(baud);
  delay(1500);
#else
  (void)baud;
  std::setbuf(stdout, nullptr);
#endif
}

// Register a handler invoked when the user asks for a clean shutdown.
// PC: SIGINT (Ctrl-C). ESP32: no signal source under arduino-esp32 — no-op.
inline void on_interrupt(void (*fn)(int)) {
#ifdef ARDUINO
  (void)fn;
#else
  std::signal(SIGINT, fn);
#endif
}

}  // namespace pal
