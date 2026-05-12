#pragma once
//
// Logger — small in-memory ring buffer of recent printf-style lines, plus a
// pass-through to stdout. The HttpServerModule's GET /api/log handler reads
// pmm::log_buffer() and returns it as JSON; the frontend polls that.
//
// Usage (drop-in replacement for std::printf for app-level messages):
//
//   pmm::log("[wifi] connected ssid=%s ip=%s\n", ssid, ip);
//
// What goes through pmm::log lands in /api/log AND on stdout. What goes
// through std::printf stays stdout-only. Migrate noisy debug prints
// gradually — only messages worth exposing to the UI need to be moved.
//
// Implementation: small fixed-size ring with mutex protection. Lines longer
// than kMaxLine are truncated. Lifetime is process-wide (anonymous-namespace
// statics) so any TU can call pmm::log without worrying about init order.
//

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace pmm {

namespace detail {

constexpr size_t kRingCapacity = 64;   // last N lines kept
constexpr size_t kMaxLine      = 192;  // truncate beyond this

inline std::mutex& log_mu_() { static std::mutex m; return m; }

inline std::vector<std::string>& log_ring_() {
  static std::vector<std::string> r;
  if (r.capacity() == 0) r.reserve(kRingCapacity);
  return r;
}

inline size_t& log_head_() { static size_t h = 0; return h; }

}  // namespace detail

// Append a line to the ring AND write it to stdout. Trailing newline in the
// format is preserved on stdout; the ring stores the line without it so the
// frontend renderer can add its own separator.
inline void log(const char* fmt, ...) {
  char line[detail::kMaxLine];
  va_list ap;
  va_start(ap, fmt);
  int n = std::vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);
  if (n < 0) return;

  std::fputs(line, stdout);
  std::fflush(stdout);

  // Strip trailing newline(s) before storing for /api/log.
  size_t len = (n < (int)sizeof(line)) ? (size_t)n : sizeof(line) - 1;
  while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) --len;

  std::lock_guard<std::mutex> lk(detail::log_mu_());
  auto& ring = detail::log_ring_();
  std::string s(line, len);
  if (ring.size() < detail::kRingCapacity) {
    ring.push_back(std::move(s));
  } else {
    ring[detail::log_head_()] = std::move(s);
    detail::log_head_() = (detail::log_head_() + 1) % detail::kRingCapacity;
  }
}

// Snapshot of buffered lines in chronological order (oldest first).
inline std::vector<std::string> log_buffer() {
  std::lock_guard<std::mutex> lk(detail::log_mu_());
  auto& ring = detail::log_ring_();
  if (ring.size() < detail::kRingCapacity) return ring;  // not yet wrapped
  std::vector<std::string> out;
  out.reserve(detail::kRingCapacity);
  size_t h = detail::log_head_();
  for (size_t i = 0; i < detail::kRingCapacity; ++i) {
    out.push_back(ring[(h + i) % detail::kRingCapacity]);
  }
  return out;
}

}  // namespace pmm
