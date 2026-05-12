#pragma once
//
// PalWs — WebSocket server platform abstraction.
//
// On ESP32 (ARDUINO): AsyncWebSocket on its own AsyncWebServer (port 81),
//   parallel to the HTTP server on port 80. Shares the WiFi task; non-blocking.
//
// On PC / rPi: minimal RFC 6455 WebSocket server on raw POSIX sockets.
//   SHA-1 + base64 inlined for the handshake (no external dependency).
//   Background accept thread + per-client handshake thread; broadcasts iterate
//   the client list and drop any fd that fails a send.
//
// Usage (both platforms):
//   pal::WsServer ws;
//   ws.begin();
//   if (ws.hasClients()) {
//     ws.broadcastText(stateJson);
//     ws.broadcastBinary(data, len);
//   }
//   ws.tick();   // ESP32: cleanup closed slots; PC: no-op
//
// Port-and-minimize log (§4 deliberation vs v1's WsServer.h, 483 LOC → 244):
//
//   DEFERRED to Sprint 4 (ESP32 OOM hardening — depends on PalHeap):
//     - Pre-allocated frame buffer infrastructure (pixBuf_, textBuf_,
//       schemaBufs_[2] + alternating index): v1 works around the lwIP
//       free-list corruption that follows WiFi STA connect. Workaround
//       for an external bug, kept in v1 because it stops real crashes;
//       v2 re-ports alongside PalHeap.h.
//     - broadcastTextBuf / broadcastSchemaBuf — write into those buffers.
//     - heap_caps_get_largest_free_block() guards in broadcast paths.
//     - PMM_MAX_PIX_FRAME / PMM_MAX_TEXT_FRAME / PMM_MAX_SCHEMA_FRAME.
//
//   DEFERRED to logging support (Sprint 5+):
//     - broadcastLog() — formats a message as a JSON log frame.
//
//   DROPPED (not a re-port target):
//     - PcSocketShims.h dependency: inlined the POSIX socket calls
//       directly. Windows path dropped — v2 targets Linux/macOS PC + ESP32.
//

#include <cstddef>
#include <cstdint>
#include <string>

#ifdef ARDUINO
  #include <AsyncWebSocket.h>
  #include <ESPAsyncWebServer.h>

  #include "PalWifi.h"
#else
  #include <arpa/inet.h>
  #include <csignal>
  #include <cstdio>
  #include <cstring>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>

  #include <mutex>
  #include <thread>
  #include <vector>

  #ifndef MSG_NOSIGNAL
    #define MSG_NOSIGNAL 0  // macOS: SIGPIPE suppressed via signal() in begin()
  #endif
#endif

namespace pal {

static constexpr uint16_t kWsPort = 81;

// ============================================================================
// ESP32 / Arduino branch
// ============================================================================

#ifdef ARDUINO

class WsServer {
 public:
  WsServer() : wsServer_(kWsPort), ws_("/") {}

  // Idempotent. AsyncServer::begin() asserts inside FreeRTOS queue ops if the
  // lwIP TCP/IP task isn't running — i.e. before WiFi/Ethernet brings up a
  // netif. Module loop1s() calls begin() once a second; the first call once
  // the network is up flips started_ true, subsequent calls return immediately.
  void begin() {
    if (started_) return;
    if (!pal::wifi_is_connected()) return;
    ws_.onEvent([](AsyncWebSocket* /*srv*/, AsyncWebSocketClient* client,
                   AwsEventType type, void* /*arg*/,
                   uint8_t* /*data*/, size_t /*len*/) {
      if (type == WS_EVT_CONNECT) printf("[WsServer] client #%u connected\n", client->id());
      else if (type == WS_EVT_DISCONNECT) printf("[WsServer] client #%u disconnected\n", client->id());
    });
    wsServer_.addHandler(&ws_);
    wsServer_.begin();
    started_ = true;
    printf("[WsServer] listening on ws://0.0.0.0:%u (network up)\n", (unsigned)kWsPort);
  }

  void broadcastText(const char* data, size_t len) {
    for (auto& c : ws_.getClients())
      if (c.status() == WS_CONNECTED && !c.queueIsFull()) c.text(data, len);
  }
  void broadcastText(const std::string& msg) { broadcastText(msg.c_str(), msg.size()); }

  void broadcastBinary(const uint8_t* data, size_t len) {
    for (auto& c : ws_.getClients())
      if (c.status() == WS_CONNECTED && !c.queueIsFull()) c.binary(data, len);
  }

  bool   hasClients() const  { return ws_.count() > 0; }
  size_t clientCount() const { return (size_t)ws_.count(); }
  void   tick()              { ws_.cleanupClients(); }

 private:
  bool           started_ = false;
  AsyncWebServer wsServer_;
  AsyncWebSocket ws_;
};

// ============================================================================
// PC / rPi branch
// ============================================================================

#else

namespace detail {

// Inline SHA-1 (FIPS 180-1) for computing the Sec-WebSocket-Accept header.
struct Sha1 {
  uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
  uint64_t bits = 0;
  uint8_t buf[64] = {};
  uint32_t len = 0;
  static uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }
  void compress(const uint8_t* b) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i)
      w[i] = ((uint32_t)b[i * 4] << 24) | ((uint32_t)b[i * 4 + 1] << 16)
           | ((uint32_t)b[i * 4 + 2] << 8) | (uint32_t)b[i * 4 + 3];
    for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], bv = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; ++i) {
      uint32_t f, k;
      if (i < 20)      { f = (bv & c) | ((~bv) & d);          k = 0x5A827999u; }
      else if (i < 40) { f = bv ^ c ^ d;                       k = 0x6ED9EBA1u; }
      else if (i < 60) { f = (bv & c) | (bv & d) | (c & d);    k = 0x8F1BBCDCu; }
      else             { f = bv ^ c ^ d;                       k = 0xCA62C1D6u; }
      uint32_t t = rol(a, 5) + f + e + k + w[i];
      e = d; d = c; c = rol(bv, 30); bv = a; a = t;
    }
    h[0] += a; h[1] += bv; h[2] += c; h[3] += d; h[4] += e;
  }
  void update(const uint8_t* data, size_t n) {
    bits += (uint64_t)n * 8;
    while (n > 0) {
      size_t room = 64 - len;
      size_t take = n < room ? n : room;
      std::memcpy(buf + len, data, take);
      len += (uint32_t)take;
      data += take;
      n -= take;
      if (len == 64) { compress(buf); len = 0; }
    }
  }
  void finish(uint8_t digest[20]) {
    buf[len++] = 0x80;
    if (len > 56) { while (len < 64) buf[len++] = 0; compress(buf); len = 0; }
    while (len < 56) buf[len++] = 0;
    for (int i = 7; i >= 0; --i) buf[56 + (7 - i)] = (bits >> (i * 8)) & 0xFF;
    compress(buf);
    for (int i = 0; i < 5; ++i) {
      digest[i * 4 + 0] = (h[i] >> 24) & 0xFF;
      digest[i * 4 + 1] = (h[i] >> 16) & 0xFF;
      digest[i * 4 + 2] = (h[i] >>  8) & 0xFF;
      digest[i * 4 + 3] =  h[i]        & 0xFF;
    }
  }
};

inline std::string base64(const uint8_t* d, size_t n) {
  static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string o;
  o.reserve(((n + 2) / 3) * 4);
  for (size_t i = 0; i < n; i += 3) {
    uint32_t v = (uint32_t)d[i] << 16
               | (i + 1 < n ? (uint32_t)d[i + 1] << 8 : 0)
               | (i + 2 < n ? (uint32_t)d[i + 2]     : 0);
    o += t[(v >> 18) & 63];
    o += t[(v >> 12) & 63];
    o += (i + 1 < n) ? t[(v >> 6) & 63] : '=';
    o += (i + 2 < n) ? t[v & 63]        : '=';
  }
  return o;
}

inline std::string acceptKey(const std::string& key) {
  std::string s = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  Sha1 sha;
  sha.update((const uint8_t*)s.data(), s.size());
  uint8_t d[20];
  sha.finish(d);
  return base64(d, 20);
}

// Encode a server→client WebSocket frame (server never masks outgoing frames).
inline std::vector<uint8_t> frame(int op, const uint8_t* data, size_t len) {
  std::vector<uint8_t> f;
  f.push_back(0x80 | (op & 0x0F));  // FIN + opcode
  if (len <= 125) {
    f.push_back((uint8_t)len);
  } else if (len <= 65535) {
    f.push_back(126);
    f.push_back((uint8_t)(len >> 8));
    f.push_back((uint8_t)(len));
  } else {
    f.push_back(127);
    for (int i = 7; i >= 0; --i) f.push_back((uint8_t)(len >> (i * 8)));
  }
  f.insert(f.end(), data, data + len);
  return f;
}

}  // namespace detail

class WsServer {
 public:
  explicit WsServer(uint16_t port = kWsPort) : port_(port) {}

  void begin() {
    if (started_) return;
    started_ = true;
    ::signal(SIGPIPE, SIG_IGN);  // prevent crash on broken pipe (macOS + Linux)

    listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) { perror("[WsServer] socket"); return; }

    int opt = 1;
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);
    if (::bind(listenFd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
      perror("[WsServer] bind");
      return;
    }
    ::listen(listenFd_, 10);

    acceptThread_ = std::thread([this] { acceptLoop(); });
    printf("[WsServer] listening on ws://0.0.0.0:%u\n", (unsigned)port_);
  }

  void broadcastText(const char* data, size_t len) {
    sendAll(detail::frame(0x01, (const uint8_t*)data, len));
  }
  void broadcastText(const std::string& msg) { broadcastText(msg.c_str(), msg.size()); }

  void broadcastBinary(const uint8_t* data, size_t len) {
    sendAll(detail::frame(0x02, data, len));
  }

  bool hasClients() const {
    std::lock_guard<std::mutex> lk(mx_);
    return !clients_.empty();
  }

  size_t clientCount() const {
    std::lock_guard<std::mutex> lk(mx_);
    return clients_.size();
  }

  void tick() {}  // no-op on PC

  ~WsServer() {
    // Close the listen socket so acceptLoop's accept() returns and the
    // thread can be joined cleanly.
    if (listenFd_ >= 0) {
      ::close(listenFd_);
      listenFd_ = -1;
    }
    if (acceptThread_.joinable()) acceptThread_.join();
    std::lock_guard<std::mutex> lk(mx_);
    for (int fd : clients_) ::close(fd);
    clients_.clear();
  }

 private:
  uint16_t port_;
  bool     started_ = false;
  int listenFd_ = -1;
  std::thread acceptThread_;
  mutable std::mutex mx_;
  std::vector<int> clients_;

  void acceptLoop() {
    while (true) {
      int fd = ::accept(listenFd_, nullptr, nullptr);
      if (fd < 0) break;
      std::thread([this, fd] { handshake(fd); }).detach();
    }
  }

  void handshake(int fd) {
  #ifdef SO_NOSIGPIPE
    int ns = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &ns, sizeof(ns));
  #endif
    // Read HTTP request headers until the blank line.
    std::string req;
    req.reserve(512);
    char c;
    while (req.size() < 8192) {
      if (::recv(fd, &c, 1, 0) <= 0) { ::close(fd); return; }
      req += c;
      if (req.size() >= 4 && req.compare(req.size() - 4, 4, "\r\n\r\n") == 0) break;
    }
    const std::string marker = "Sec-WebSocket-Key: ";
    auto p = req.find(marker);
    if (p == std::string::npos) { ::close(fd); return; }
    p += marker.size();
    auto e = req.find("\r\n", p);
    std::string key = req.substr(p, e - p);

    std::string resp = "HTTP/1.1 101 Switching Protocols\r\n"
                       "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                       "Sec-WebSocket-Accept: " + detail::acceptKey(key) + "\r\n\r\n";
    if (::send(fd, resp.data(), resp.size(), MSG_NOSIGNAL) <= 0) { ::close(fd); return; }

    {
      std::lock_guard<std::mutex> lk(mx_);
      clients_.push_back(fd);
    }
    printf("[WsServer] client connected fd=%d\n", fd);
  }

  void sendAll(const std::vector<uint8_t>& f) {
    std::lock_guard<std::mutex> lk(mx_);
    std::vector<int> alive;
    for (int fd : clients_) {
      if (::send(fd, f.data(), f.size(), MSG_NOSIGNAL) > 0) {
        alive.push_back(fd);
      } else {
        ::close(fd);
        printf("[WsServer] client disconnected fd=%d\n", fd);
      }
    }
    clients_ = std::move(alive);
  }
};

#endif  // ARDUINO

}  // namespace pal
