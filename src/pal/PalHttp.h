#pragma once
// HttpServer — thin platform abstraction over the HTTP server.
//
// On ESP32 (ARDUINO): wraps ESPAsyncWebServer.
//   - Non-blocking; callbacks run on the WiFi task.
//   - begin() starts the server; no blocking listen() needed.
//
// On PC / rPi: wraps cpp-httplib.
//   - listen() blocks; run it in a std::thread (started by begin()).
//   - Callbacks run on cpp-httplib's internal thread pool.
//
// Usage (both platforms):
//   HttpServer server(80);
//   server.onGet ("/path",   [](const std::string& body) -> HttpResponse { ... });
//   server.onPost("/path",   [](const std::string& body) -> HttpResponse { ... });
//   server.begin();
//
// HttpResponse carries status code, content-type, and body string.
// The same lambda signature works identically on both platforms.

#include <functional>
#include <string>

namespace pal {

struct HttpResponse {
  HttpResponse() : status(200), contentType("application/json") {}
  HttpResponse(int s, std::string ct, std::string b) : status(s), contentType(std::move(ct)), body(std::move(b)) {}

  int status;
  std::string contentType;
  std::string body;
};

using HttpHandler = std::function<HttpResponse(const std::string& body)>;

// Handler for DELETE requests: receives the full request path so callers can
// extract path parameters (e.g. "/api/modules/sine1" → id = "sine1").
using HttpDeleteHandler = std::function<HttpResponse(const std::string& path)>;

// Handler for PATCH requests: receives both the full path (for id extraction)
// and the request body.
using HttpPatchHandler = std::function<HttpResponse(const std::string& path, const std::string& body)>;

// Handlers for binary POST (e.g. firmware upload).
// onChunk is called for each incoming chunk; returning false aborts and sets ok=false for onEnd.
// onEnd is called once after all chunks (or on error); ok=false means at least one chunk failed.
using BinaryChunkFn = std::function<bool(const uint8_t* data, size_t len, size_t offset, size_t total)>;
using BinaryEndFn = std::function<HttpResponse(bool ok)>;

}  // namespace pal

// ---- Platform implementations ------------------------------------------------

#ifdef ARDUINO

  #include <ESPAsyncWebServer.h>

  #include <map>
  #include <set>
  #include <vector>

namespace pal {

class HttpServer {
 public:
  explicit HttpServer(uint16_t port) : server_(port) {}

  void onGet(const char* path, HttpHandler handler) {
    server_.on(path, HTTP_GET, [handler](AsyncWebServerRequest* req) {
      try {
        auto resp = handler("");
        req->send(resp.status, resp.contentType.c_str(), resp.body.c_str());
      } catch (const std::bad_alloc&) {
        req->send(503, "application/json", R"({"error":"low heap"})");
      }
    });
  }

  // Serve a gzip-compressed byte array with Content-Encoding: gzip.
  // beginResponse_P streams directly from rodata — no heap copy of the payload.
  void onGetStaticGzip(const char* path, const char* contentType, const uint8_t* data, size_t len) {
    server_.on(path, HTTP_GET, [contentType, data, len](AsyncWebServerRequest* req) {
      AsyncWebServerResponse* resp = req->beginResponse(200, String(contentType), data, len);
      resp->addHeader("Content-Encoding", "gzip");
      req->send(resp);
    });
  }

  // onPost with a body: ESPAsyncWebServer needs a body handler.
  // The path is stored; the body handler matches it.
  void onPost(const char* path, HttpHandler handler) {
    // Store path+handler so the body-complete callback can find it.
    postHandlers_.push_back({std::string(path), handler});

    server_.on(
        path, HTTP_POST,
        // Request-complete callback (fired after body is assembled).
        [this](AsyncWebServerRequest* req) {
          const std::string p = req->url().c_str();
          std::string body;
          // Body was collected in the body-bytes callback below.
          if (bodyBuf_.count(req)) {
            body = bodyBuf_[req];
            bodyBuf_.erase(req);
          }
          for (auto& ph : postHandlers_) {
            if (ph.path == p) {
              try {
                auto resp = ph.handler(body);
                req->send(resp.status, resp.contentType.c_str(), resp.body.c_str());
              } catch (const std::bad_alloc&) {
                req->send(503, "application/json", R"({"error":"low heap"})");
              }
              return;
            }
          }
          req->send(404);
        },
        nullptr,  // upload handler (unused)
        // Body bytes callback.
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) { bodyBuf_[req] += std::string((char*)data, len); });
  }

  void onDelete(const char* path, HttpDeleteHandler handler) {
    // ESPAsyncWebServer uses trailing '*' for prefix-wildcard matching, not
    // regex. Convert a trailing "/.+" (used on the PC side with httplib) to
    // "/*" so DELETE /api/modules/<id> is matched correctly.
    std::string pattern(path);
    if (pattern.size() >= 3 && pattern.substr(pattern.size() - 3) == "/.+") pattern.replace(pattern.size() - 3, 3, "/*");
    server_.on(pattern.c_str(), HTTP_DELETE, [handler](AsyncWebServerRequest* req) {
      try {
        auto resp = handler(req->url().c_str());
        req->send(resp.status, resp.contentType.c_str(), resp.body.c_str());
      } catch (const std::bad_alloc&) {
        req->send(503, "application/json", R"({"error":"low heap"})");
      }
    });
  }

  // PATCH with body: same body-collection mechanism as POST, handler receives
  // both the full request path (for id extraction) and the body.
  void onPatch(const char* pathPattern, HttpPatchHandler handler) {
    std::string prefix(pathPattern);
    // Strip trailing "/.+" to get the base prefix for matching.
    if (prefix.size() >= 3 && prefix.substr(prefix.size() - 3) == "/.+") prefix.resize(prefix.size() - 3);
    patchHandlers_.push_back({prefix, handler});
    std::string espPattern(pathPattern);
    if (espPattern.size() >= 3 && espPattern.substr(espPattern.size() - 3) == "/.+") espPattern.replace(espPattern.size() - 3, 3, "/*");
    server_.on(
        espPattern.c_str(), HTTP_PATCH,
        [this](AsyncWebServerRequest* req) {
          const std::string p = req->url().c_str();
          std::string body;
          if (bodyBuf_.count(req)) {
            body = bodyBuf_[req];
            bodyBuf_.erase(req);
          }
          for (auto& ph : patchHandlers_) {
            if (p.rfind(ph.prefix, 0) == 0) {
              try {
                auto resp = ph.handler(p, body);
                req->send(resp.status, resp.contentType.c_str(), resp.body.c_str());
              } catch (const std::bad_alloc&) {
                req->send(503, "application/json", R"({"error":"low heap"})");
              }
              return;
            }
          }
          req->send(404);
        },
        nullptr, [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) { bodyBuf_[req] += std::string((char*)data, len); });
  }

  // Binary POST: streams incoming body chunks directly to onChunk without buffering.
  // Designed for large uploads (e.g. firmware binaries) where buffering in RAM is not viable.
  void onPostBinary(const char* path, BinaryChunkFn onChunk, BinaryEndFn onEnd) {
    binaryHandlers_.push_back({std::string(path), onChunk, onEnd});
    server_.on(
        path, HTTP_POST,
        // Request-complete callback: fires after all body chunks have been received.
        [this](AsyncWebServerRequest* req) {
          const std::string p = req->url().c_str();
          bool ok = true;
          if (binaryFailed_.count(req)) {
            ok = false;
            binaryFailed_.erase(req);
          }
          for (auto& bh : binaryHandlers_) {
            if (bh.path == p) {
              auto resp = bh.onEnd(ok);
              req->send(resp.status, resp.contentType.c_str(), resp.body.c_str());
              return;
            }
          }
          req->send(404);
        },
        nullptr,  // upload handler (not used for application/octet-stream)
        // Body callback: called for each incoming chunk; index=offset, total=content-length.
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
          const std::string p = req->url().c_str();
          for (auto& bh : binaryHandlers_) {
            if (bh.path == p) {
              if (!bh.onChunk(data, len, index, total)) binaryFailed_.insert(req);
              return;
            }
          }
        });
  }

  void begin() { server_.begin(); }

 private:
  AsyncWebServer server_;
  struct PostEntry {
    std::string path;
    HttpHandler handler;
  };
  struct PatchEntry {
    std::string prefix;
    HttpPatchHandler handler;
  };
  struct BinaryEntry {
    std::string path;
    BinaryChunkFn onChunk;
    BinaryEndFn onEnd;
  };
  std::vector<PostEntry> postHandlers_;
  std::vector<PatchEntry> patchHandlers_;
  std::vector<BinaryEntry> binaryHandlers_;
  std::map<AsyncWebServerRequest*, std::string> bodyBuf_;
  std::set<AsyncWebServerRequest*> binaryFailed_;
};

}  // namespace pal

#else  // PC / rPi

  // Disable SSL and macOS keychain cert loading — plain HTTP only.
  #undef CPPHTTPLIB_OPENSSL_SUPPORT
  #undef CPPHTTPLIB_USE_CERTS_FROM_MACOSX_KEYCHAIN
  #include <httplib.h>

  #include <thread>

namespace pal {

class HttpServer {
 public:
  explicit HttpServer(uint16_t port) : port_(port) {}

  void onGet(const char* path, HttpHandler handler) {
    server_.Get(path, [handler](const httplib::Request& /*req*/, httplib::Response& res) {
      auto resp = handler("");
      res.status = resp.status;
      res.set_content(resp.body, resp.contentType.c_str());
    });
  }

  void onGetStaticGzip(const char* path, const char* contentType, const uint8_t* data, size_t len) {
    server_.Get(path, [contentType, data, len](const httplib::Request& /*req*/, httplib::Response& res) {
      res.status = 200;
      res.set_header("Content-Encoding", "gzip");
      res.set_content(reinterpret_cast<const char*>(data), len, contentType);
    });
  }

  void onPost(const char* path, HttpHandler handler) {
    server_.Post(path, [handler](const httplib::Request& req, httplib::Response& res) {
      auto resp = handler(req.body);
      res.status = resp.status;
      res.set_content(resp.body, resp.contentType.c_str());
    });
  }

  // Register a DELETE handler. The full request path is passed to the handler
  // so path parameters can be extracted (e.g. "/api/modules/sine1").
  // pathPattern is a regex — use e.g. "/api/modules/(.+)" to match any id.
  void onDelete(const char* pathPattern, HttpDeleteHandler handler) {
    server_.Delete(pathPattern, [handler](const httplib::Request& req, httplib::Response& res) {
      auto resp = handler(req.path);
      res.status = resp.status;
      res.set_content(resp.body, resp.contentType.c_str());
    });
  }

  // Register a PATCH handler. Receives both path (for id extraction) and body.
  void onPatch(const char* pathPattern, HttpPatchHandler handler) {
    server_.Patch(pathPattern, [handler](const httplib::Request& req, httplib::Response& res) {
      auto resp = handler(req.path, req.body);
      res.status = resp.status;
      res.set_content(resp.body, resp.contentType.c_str());
    });
  }

  // Binary POST: on PC the body is buffered by httplib; onChunk is called once then onEnd.
  void onPostBinary(const char* path, BinaryChunkFn onChunk, BinaryEndFn onEnd) {
    server_.Post(path, [onChunk, onEnd](const httplib::Request& req, httplib::Response& res) {
      const auto& body = req.body;
      bool ok = onChunk(reinterpret_cast<const uint8_t*>(body.data()), body.size(), 0, body.size());
      auto resp = onEnd(ok);
      res.status = resp.status;
      res.set_content(resp.body, resp.contentType.c_str());
    });
  }

  // Starts listening in a background thread; returns immediately.
  // Prints an error and skips the thread if the port cannot be bound
  // (e.g. port < 1024 without elevated privileges, or port already in use).
  void begin() {
    if (!server_.bind_to_port("0.0.0.0", port_)) {
      fprintf(stderr,
              "[HttpServer] ERROR: cannot bind port %u"
              " (port in use, or requires elevated privileges)\n",
              port_);
      return;
    }
    // Run listen_after_bind() on a background thread so begin() returns
    // immediately.  The thread is stored (not detached) so ~HttpServer()
    // can join it after server_.stop(), ensuring no use-after-free.
    thread_ = std::thread([this]() {
      try {
        server_.listen_after_bind();
      } catch (...) {
        fprintf(stderr, "[HttpServer] ERROR: listen_after_bind threw\n");
      }
    });
    printf("[HttpServer] listening on http://0.0.0.0:%u\n", port_);
  }

  ~HttpServer() {
    server_.stop();                          // signal server to stop accepting
    if (thread_.joinable()) thread_.join();  // wait for listen thread to exit
  }

 private:
  httplib::Server server_;
  uint16_t port_;
  std::thread thread_;
};

}  // namespace pal

#endif  // ARDUINO
