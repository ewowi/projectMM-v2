#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include <doctest/doctest.h>

#include "core/ModuleManager.h"
#include "modules/hello/HelloModule.h"
#include "modules/network/HttpServerModule.h"

// HttpServerModule integration tests: drive the real pal::HttpServer (which
// wraps cpp-httplib on PC) and probe it with a minimal raw-TCP client.
//
// Why raw TCP and not httplib::Client? cpp-httplib's client misbehaves when
// used in the same process as its server (consistent "Failed to read
// connection" on the first request, both with and without delays). Raw TCP
// against the same server returns the full response. The test contract is
// behavioural — "the server answers HTTP" — so a 30-line raw client suffices
// and is independent of cpp-httplib's same-process client quirks.

using namespace pmm;

namespace {

constexpr int kPortFrontend = 18080;
constexpr int kPortListing  = 18081;
constexpr int kPort404      = 18082;

struct HttpReply {
  bool ok = false;
  int status = -1;
  std::string headers;   // raw header block
  std::string body;
};

HttpReply http_get(int port, const std::string& path) {
  HttpReply r;
  int s = socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0) return r;
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port));
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    close(s);
    return r;
  }
  std::string req = "GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                    "Connection: close\r\n\r\n";
  if (send(s, req.data(), req.size(), 0) != static_cast<ssize_t>(req.size())) {
    close(s);
    return r;
  }
  std::string buf;
  char tmp[4096];
  while (true) {
    ssize_t n = recv(s, tmp, sizeof(tmp), 0);
    if (n <= 0) break;
    buf.append(tmp, static_cast<size_t>(n));
  }
  close(s);
  auto split = buf.find("\r\n\r\n");
  if (split == std::string::npos) return r;
  r.headers = buf.substr(0, split);
  r.body = buf.substr(split + 4);
  // Parse "HTTP/1.1 NNN ...\r\n" status line.
  auto sp1 = r.headers.find(' ');
  if (sp1 == std::string::npos) return r;
  r.status = std::atoi(r.headers.c_str() + sp1 + 1);
  r.ok = (r.status > 0);
  return r;
}

// pal::HttpServer::begin() returns after bind_to_port; the listen thread then
// has to call listen_after_bind() before accept() succeeds. Poll until the
// listener actually accepts a TCP connection.
void await_listener(int port) {
  for (int i = 0; i < 100; ++i) {  // up to ~5 s
    int s = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    int rc = connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    close(s);
    if (rc == 0) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}

}  // namespace

TEST_CASE("HttpServerModule: GET / serves the gzipped frontend bundle") {
  ModuleManager mm;
  mm.register_type("http", [] { return std::make_unique<HttpServerModule>(kPortFrontend); });
  mm.add("http", "http-fe");
  await_listener(kPortFrontend);

  auto r = http_get(kPortFrontend, "/");
  REQUIRE(r.ok);
  CHECK(r.status == 200);
  CHECK(r.headers.find("Content-Type: text/html") != std::string::npos);
  CHECK(r.headers.find("Content-Encoding: gzip") != std::string::npos);
  // Bundle is ~24 KB; assert real content + the gzip magic header (1f 8b).
  REQUIRE(r.body.size() > 1000);
  CHECK(static_cast<unsigned char>(r.body[0]) == 0x1f);
  CHECK(static_cast<unsigned char>(r.body[1]) == 0x8b);
}

TEST_CASE("HttpServerModule: GET /api/modules lists registered modules") {
  ModuleManager mm;
  mm.register_type("hello", [] { return std::make_unique<HelloModule>(); });
  mm.register_type("http",  [] { return std::make_unique<HttpServerModule>(kPortListing); });
  mm.add("hello", "hello-t");
  mm.add("http",  "http-t");
  await_listener(kPortListing);

  auto r = http_get(kPortListing, "/api/modules");
  REQUIRE(r.ok);
  CHECK(r.status == 200);
  CHECK(r.headers.find("Content-Type: application/json") != std::string::npos);
  CHECK(r.body.find("\"id\":\"hello-t\"") != std::string::npos);
  CHECK(r.body.find("\"type\":\"hello\"") != std::string::npos);
  CHECK(r.body.find("\"id\":\"http-t\"")  != std::string::npos);
}

TEST_CASE("HttpServerModule: unknown route returns 404") {
  ModuleManager mm;
  mm.register_type("http", [] { return std::make_unique<HttpServerModule>(kPort404); });
  mm.add("http", "http-404");
  await_listener(kPort404);

  auto r = http_get(kPort404, "/no-such-path");
  REQUIRE(r.ok);
  CHECK(r.status == 404);
}
