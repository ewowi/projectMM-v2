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

// Default HTTP port. ESP32 binds 80 freely — users open http://<device-ip>
// without a :port suffix. PC needs root for ports < 1024, and run.py is
// unprivileged, so we use 8080 there. Override with the HttpServerModule
// constructor arg if you need a different port (e.g. running two PC
// instances side by side).
inline uint16_t default_http_port() {
#ifdef ARDUINO
  return 80;
#else
  return 8080;
#endif
}

// How many scheduler threads make sense on this platform. Both PC and
// ESP32 now run 2: PC has hardware threads to spare; ESP32 uses
// pal::task_create_pinned with 8 KB stacks per core (Sprint 7 PalRtos —
// fixes Sprint 5's pthread-stack-overflow workaround that pinned ESP32
// to a single inline core).
inline int default_scheduler_cores() { return 2; }

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
