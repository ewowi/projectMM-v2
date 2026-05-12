#include <doctest/doctest.h>
#include <memory>
#include <string>

#include "core/ModuleManager.h"
#include "modules/hello/HelloModule.h"

using namespace pmm;

TEST_CASE("HelloModule: counter increments when enabled") {
  HelloModule h;
  h.loop1s();
  h.loop1s();
  CHECK(h.counter_ == 2);
}

TEST_CASE("HelloModule: counter pauses when disabled") {
  HelloModule h;
  h.enabled_ = false;
  h.loop1s();
  CHECK(h.counter_ == 0);
}

TEST_CASE("HelloModule: serialize_json contains counter and id") {
  ModuleManager mm;
  mm.register_type("hello", [] { return std::make_unique<HelloModule>(); });
  auto* h = static_cast<HelloModule*>(mm.add("hello", "h0"));
  h->loop1s();
  std::string out;
  h->serialize_json(out);
  CHECK(out.find("\"counter\":1") != std::string::npos);
  CHECK(out.find("\"id\":\"h0\"") != std::string::npos);
  CHECK(out.find("\"enabled\":true") != std::string::npos);
}
