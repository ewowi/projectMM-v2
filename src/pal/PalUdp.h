#pragma once
//
// PalUdp — minimal UDP send abstraction. ESP32: AsyncUDP::writeTo —
// fire-and-forget, no EHOSTUNREACH log spam on unreachable destinations.
// PC: BSD SOCK_DGRAM. No receive support yet — ArtNet-in (Release 2) adds it.
//
// API:
//   pal::Udp udp;
//   udp.begin();                                  // no-op on ESP32, bind on PC
//   udp.send("192.168.1.255", 6454, data, len);   // best-effort, broadcast OK
//

#include <cstddef>
#include <cstdint>
#include <cstring>

#ifdef ARDUINO

  #include <AsyncUDP.h>
  #include <WiFi.h>

  #include "PalWifi.h"

namespace pal {

class Udp {
 public:
  bool begin() { return true; }  // AsyncUDP needs no bind for send-only use

  bool send(const char* ip, uint16_t port, const uint8_t* data, size_t len) {
    if (!pal::wifi_is_connected()) return false;  // PATCH: wifi-guard
    IPAddress addr;
    if (!addr.fromString(ip)) return false;
    return udp_.writeTo(data, len, addr, port) == len;
  }

 private:
  AsyncUDP udp_;
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
