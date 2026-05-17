#pragma once
//
// PalUdp — minimal send-only UDP. Both platforms use a plain BSD-style
// SOCK_DGRAM socket: ESP32 via lwIP's BSD sockets layer, PC via the OS.
// The two branches differ only by which sockets header they include (and
// the ESP32 wifi-guard) — destruction is one bounded ::close() on both, so
// a module owning a pal::Udp can be deleted from any task without the
// cross-task-blocking ~AsyncUDP() teardown that previously needed deferral
// (see ADR 0005; the AsyncUDP variant was the wrong abstraction for a
// send-only socket). No receive support yet — ArtNet-in (Release 2) adds it.
//
// API:
//   pal::Udp udp;
//   udp.begin();                                  // open the socket
//   udp.send("192.168.1.255", 6454, data, len);   // best-effort, broadcast OK
//

#include <cstddef>
#include <cstdint>
#include <cstring>

#ifdef ARDUINO
  #include <lwip/sockets.h>

  #include "PalWifi.h"
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
#endif

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
#ifdef ARDUINO
    if (!pal::wifi_is_connected()) return false;  // PATCH: wifi-guard
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    if (::inet_pton(AF_INET, ip, &addr.sin_addr) != 1) return false;
    return ::sendto(fd_, data, len, 0, (sockaddr*)&addr, sizeof(addr)) == (ssize_t)len;
  }

  // One bounded lwIP/OS close — safe on any task (this is the whole point
  // of the AsyncUDP→plain-socket swap; no cross-task teardown block).
  ~Udp() {
    if (fd_ >= 0) ::close(fd_);
  }

 private:
  int fd_ = -1;
};

}  // namespace pal
