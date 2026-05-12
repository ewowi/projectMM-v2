#pragma once
//
// PalFs — small filesystem wrapper used for read/write of tiny JSON files
// (WiFi creds, per-module state). ESP32: LittleFS. PC: std::filesystem with
// a `state/` directory under the CWD as the root (so paths from the firmware
// — always starting with `/` — resolve to a sandbox the PC build can touch
// without colliding with system paths).
//
// API is minimal on purpose. Per CLAUDE.md Rule #1, larger filesystem
// concerns (listing, mkdir, streaming reads) land only when a caller needs
// them. The current callers (WifiStaModule, future per-module state) just
// need read_text / write_text / exists.
//
// Mounting is lazy: the first call begins LittleFS, formatting on first
// boot if the partition is uninitialised. Subsequent calls are O(1) on a
// cached flag.
//

#include <string>

#ifdef ARDUINO
  #include <LittleFS.h>
#else
  #include <filesystem>
  #include <fstream>
  #include <sstream>
#endif

namespace pal {

#ifdef ARDUINO

inline bool fs_ensure_mounted_() {
  static bool mounted = false;
  if (mounted) return true;
  // format_on_fail=true: on a virgin partition the first mount fails, this
  // formats it once, then succeeds — losing nothing because there's nothing.
  mounted = LittleFS.begin(/*format_on_fail=*/true);
  return mounted;
}

// LittleFS requires absolute paths (leading '/'). Accept either form.
inline std::string fs_path_(const char* path) {
  return (path && path[0] == '/') ? std::string(path) : (std::string("/") + (path ? path : ""));
}

inline bool fs_exists(const char* path) {
  if (!fs_ensure_mounted_()) return false;
  return LittleFS.exists(fs_path_(path).c_str());
}

inline std::string fs_read_text(const char* path) {
  if (!fs_ensure_mounted_()) return {};
  std::string p = fs_path_(path);
  if (!LittleFS.exists(p.c_str())) return {};
  File f = LittleFS.open(p.c_str(), "r");
  if (!f) return {};
  std::string s;
  s.reserve(f.size());
  while (f.available()) s += static_cast<char>(f.read());
  f.close();
  return s;
}

inline bool fs_write_text(const char* path, const std::string& content) {
  if (!fs_ensure_mounted_()) return false;
  File f = LittleFS.open(fs_path_(path).c_str(), "w");
  if (!f) return false;
  f.print(content.c_str());
  f.close();
  return true;
}

#else  // PC

// PC root: a `state/` directory under CWD. Created on demand. Firmware paths
// like "/wifi.json" map to ./state/wifi.json so the PC build can read/write
// without escaping into the user's home directory.
inline std::filesystem::path fs_root_() {
  auto root = std::filesystem::current_path() / "state";
  std::filesystem::create_directories(root);
  return root;
}

inline std::filesystem::path fs_path_(const char* path) {
  const char* p = (path && path[0] == '/') ? path + 1 : (path ? path : "");
  return fs_root_() / p;
}

inline bool fs_exists(const char* path) {
  return std::filesystem::exists(fs_path_(path));
}

inline std::string fs_read_text(const char* path) {
  std::ifstream f(fs_path_(path));
  if (!f) return {};
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

inline bool fs_write_text(const char* path, const std::string& content) {
  std::ofstream f(fs_path_(path));
  if (!f) return false;
  f << content;
  return static_cast<bool>(f);
}

#endif

}  // namespace pal
