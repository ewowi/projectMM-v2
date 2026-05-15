#pragma once
//
// PalUdp — minimal UDP send abstraction. ESP32: wraps WiFiUDP. PC: BSD
// SOCK_DGRAM. No receive support yet — its first caller (Sprint 6's
// ArtnetOutModule) is send-only. Receive lands when ArtNet-in does in
// Sprint 8 (same pattern as PalHttp: grow capabilities with callers).
//
// API:
//   pal::Udp udp;
//   udp.begin();                                  // bind ephemeral source port
//   udp.send("192.168.1.255", 6454, data, len);   // best-effort, broadcast OK
//
// `send` returns true on a successful enqueue (kernel accepted the packet),
// false if the socket isn't initialised or the OS refused. Broadcast is
// allowed by default on both branches — Art-Net's discovery and the Sprint 6
// default of 255.255.255.255 need it.
//

#include <cstddef>
#include <cstdint>
#include <cstring>

#ifdef ARDUINO

  #include <WiFi.h>
  #include <WiFiUdp.h>

  #include "PalWifi.h"

namespace pal {

class Udp {
 public:
  bool begin() {
    if (started_) return true;
    // begin(uint16_t) binds a local port for receiving; we pass 0 to let the
    // stack pick. Returns 1 on success.
    started_ = (udp_.begin((uint16_t)0) == 1);
    return started_;
  }

  bool send(const char* ip, uint16_t port, const uint8_t* data, size_t len) {
    // PATCH: wifi-guard + WiFiUDP — guards against pre-WiFi sends and hides
    // EHOSTUNREACH spam from WiFiUDP::endPacket(). Remove when AsyncUDP lands
    // (ArtNet-in, Release 2): AsyncUDP::writeTo() is fire-and-forget, never blocks.
    if (!started_ || !pal::wifi_is_connected()) return false;
    if (!udp_.beginPacket(ip, port)) return false;
    udp_.write(data, len);
    return udp_.endPacket() == 1;
  }

 private:
  WiFiUDP udp_;
  bool    started_ = false;
};

}  // namespace pal

#else  // PC

  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>

namespace pal {

class Udp {
 public:
  bool begin() {
    if (fd_ >= 0) return true;
    fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) return false;
    int yes = 1;
    setsockopt(fd_, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
    return true;
  }

  bool send(const char* ip, uint16_t port, const uint8_t* data, size_t len) {
    if (fd_ < 0) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    if (::inet_pton(AF_INET, ip, &addr.sin_addr) != 1) return false;
    return ::sendto(fd_, data, len, 0, (sockaddr*)&addr, sizeof(addr)) == (ssize_t)len;
  }

  ~Udp() {
    if (fd_ >= 0) ::close(fd_);
  }

 private:
  int fd_ = -1;
};

}  // namespace pal

#endif
