#include "MoonModule.h"

#include <cstdlib>

#include "../pal/Pal.h"
#include "../modules/system/MemTracker.h"

namespace pmm {

// -- Destructor --------------------------------------------------------------

MoonModule::~MoonModule() {
  freeOwnedOptions_();
  delete[] controls_;
  delete[] children_;  // array only — children are owned by ModuleManager
  delete pendingProps_;
}

// -- Dispatch wrappers -------------------------------------------------------

void MoonModule::runSetup() {
  // Full wipe before hot-reload: bypass clearControls() to avoid touching
  // pendingProps_ (already holds values from loadState()) and avoid setting
  // schemaDirty_.
  freeOwnedOptions_();
  controlCount_ = 0;
  // Sprint 8 Rail 2: bracket the setup phase with MemBoot (setup cost only)
  // and MemLive (setup + children + onAllocateMemory). Both flow through
  // pmm::log → /api/log → ui.py log window.
  const auto before = memtracker::snapshot();
  setup();
  memtracker::log_boot(id(), before);
  onBuildControls();  // module registers its addControl() calls here
  // Register enabled_ AFTER onBuildControls() so it picks up any pending value.
  addControl(enabled_, "enabled", "toggle");
  controls_[controlCount_ - 1].system = true;
  // Move enabled_ to index 0 so the UI card always shows it first.
  if (controlCount_ > 1) {
    ControlDescriptor enabledDesc = controls_[controlCount_ - 1];
    std::memmove(&controls_[1], &controls_[0], (controlCount_ - 1) * sizeof(ControlDescriptor));
    controls_[0] = enabledDesc;
  }
  for (uint8_t i = 0; i < childCount_; ++i) children_[i]->runSetup();
  onChildrenReady();
  onAllocateMemory();     // settle dynamic buffers once all params are known
  delete pendingProps_;   // release pending store memory
  pendingProps_ = nullptr;
  memtracker::log_live(id(), before);
}

void MoonModule::runLoop() {
  if (enabled_) loop();
  ++tickCount_;  // sampled in runLoop1s → ms_per_tick → fps in the UI
  for (uint8_t i = 0; i < childCount_; ++i) children_[i]->runLoop();
}

void MoonModule::runLoop20ms() {
  if (enabled_) loop20ms();
  for (uint8_t i = 0; i < childCount_; ++i) children_[i]->runLoop20ms();
}

void MoonModule::runLoop1s() {
  // Sample tick rate. timingPrevMs_ starts at 0 (first sample seeds it).
  const uint32_t now = pal::millis();
  if (timingPrevMs_ != 0 && tickCount_ > timingPrevTicks_) {
    const uint32_t dt = (now - timingPrevMs_) * 1000u;
    const uint32_t dn = tickCount_ - timingPrevTicks_;
    usPerTick_ = (uint16_t)(dt / dn > 65535u ? 65535u : dt / dn);
  }
  timingPrevMs_    = now;
  timingPrevTicks_ = tickCount_;
  if (enabled_) loop1s();
  for (uint8_t i = 0; i < childCount_; ++i) children_[i]->runLoop1s();
}

void MoonModule::runLoop10s() {
  if (!enabled_) {
    for (uint8_t i = 0; i < childCount_; ++i) children_[i]->runLoop10s();
    return;
  }
  loop10s();
  for (uint8_t i = 0; i < childCount_; ++i) children_[i]->runLoop10s();
}

void MoonModule::runTeardown() {
  // Children release their references first (they may point into parent resources).
  for (uint8_t i = 0; i < childCount_; ++i) children_[i]->runTeardown();
  teardown();
}

// -- State persistence -------------------------------------------------------

void MoonModule::setProps(JsonObjectConst props) {
  if (!pendingProps_) pendingProps_ = new JsonDocument();
  for (auto kv : props) (*pendingProps_)[kv.key()] = kv.value();
}

void MoonModule::loadState(JsonObjectConst state) {
  if (!pendingProps_) pendingProps_ = new JsonDocument();
  for (auto kv : state) (*pendingProps_)[kv.key()] = kv.value();
}

void MoonModule::saveState(JsonObject state) const {
  for (uint8_t i = 0; i < controlCount_; ++i) {
    const ControlDescriptor& d = controls_[i];
    if (std::strcmp(d.key, "enabled") == 0) continue;  // handled by saveBaseState
    switch (d.type) {
      case CtrlType::Float:      state[d.key] = *reinterpret_cast<const float*>(d.ptr); break;
      case CtrlType::Uint8:      state[d.key] = (int)*reinterpret_cast<const uint8_t*>(d.ptr); break;
      case CtrlType::Uint16:     state[d.key] = (int)*reinterpret_cast<const uint16_t*>(d.ptr); break;
      case CtrlType::Uint32:     state[d.key] = *reinterpret_cast<const uint32_t*>(d.ptr); break;
      case CtrlType::Bool:       state[d.key] = *reinterpret_cast<const bool*>(d.ptr); break;
      case CtrlType::EditStr:    state[d.key] = reinterpret_cast<const char*>(d.ptr); break;
      case CtrlType::Select:     state[d.key] = (int)*reinterpret_cast<const uint8_t*>(d.ptr); break;
      case CtrlType::String:     break;  // display-only
      case CtrlType::FloatConst: break;  // static display
    }
  }
}

// -- Control registration ----------------------------------------------------

void MoonModule::addControl(float& v, const char* k, const char* u, float lo, float hi) {
  ensureCapacity_();
  controls_[controlCount_++] = {k, u, CtrlType::Float, reinterpret_cast<uintptr_t>(&v), lo, hi, v, nullptr, 0, false, false};
  applyPending_(controls_[controlCount_ - 1]);
}

void MoonModule::addControl(uint8_t& v, const char* k, const char* u, uint8_t lo, uint8_t hi) {
  ensureCapacity_();
  controls_[controlCount_++] = {k, u, CtrlType::Uint8, reinterpret_cast<uintptr_t>(&v), (float)lo, (float)hi, (float)v, nullptr, 0, false, false};
  applyPending_(controls_[controlCount_ - 1]);
}

void MoonModule::addControl(uint16_t& v, const char* k, const char* u, uint16_t lo, uint16_t hi) {
  ensureCapacity_();
  controls_[controlCount_++] = {k, u, CtrlType::Uint16, reinterpret_cast<uintptr_t>(&v), (float)lo, (float)hi, (float)v, nullptr, 0, false, false};
  applyPending_(controls_[controlCount_ - 1]);
}

void MoonModule::addControl(uint32_t& v, const char* k, const char* u, uint32_t lo, uint32_t hi) {
  ensureCapacity_();
  controls_[controlCount_++] = {k, u, CtrlType::Uint32, reinterpret_cast<uintptr_t>(&v), (float)lo, (float)hi, (float)v, nullptr, 0, false, false};
  applyPending_(controls_[controlCount_ - 1]);
}

void MoonModule::addControl(bool& v, const char* k, const char* u, float lo, float hi) {
  ensureCapacity_();
  controls_[controlCount_++] = {k, u, CtrlType::Bool, reinterpret_cast<uintptr_t>(&v), lo, hi, v ? 1.0f : 0.0f, nullptr, 0, false, false};
  applyPending_(controls_[controlCount_ - 1]);
}

void MoonModule::addControl(char* v, const char* k, const char* u) {
  ensureCapacity_();
  controls_[controlCount_++] = {k, u, CtrlType::String, reinterpret_cast<uintptr_t>(v), 0.0f, 0.0f, 0.0f, nullptr, 0, false, false};
}

void MoonModule::addControl(float&& value, const char* k, const char* u, float lo, float hi) {
  ensureCapacity_();
  controls_[controlCount_++] = {k, u, CtrlType::FloatConst, 0, lo, hi, value, nullptr, 0, false, false};
}
void MoonModule::addControl(int8_t&& value, const char* k, const char* u, int8_t lo, int8_t hi) {
  addControl((float)value, k, u, (float)lo, (float)hi);
}
void MoonModule::addControl(uint8_t&& value, const char* k, const char* u, uint8_t lo, uint8_t hi) {
  addControl((float)value, k, u, (float)lo, (float)hi);
}
void MoonModule::addControl(uint16_t&& value, const char* k, const char* u, uint16_t lo, uint16_t hi) {
  addControl((float)value, k, u, (float)lo, (float)hi);
}
void MoonModule::addControl(uint32_t&& value, const char* k, const char* u, uint32_t lo, uint32_t hi) {
  addControl((float)value, k, u, (float)lo, (float)hi);
}

void MoonModule::addControl(uint8_t& v, const char* k, const char* const* opts, uint8_t count) {
  ensureCapacity_();
  controls_[controlCount_++] = {k, "select", CtrlType::Select, reinterpret_cast<uintptr_t>(&v), 0.0f, (float)(count - 1), (float)v, const_cast<const char**>(opts), count, false, false};
  applyPending_(controls_[controlCount_ - 1]);
}

void MoonModule::addControl(uint8_t& v, const char* k, const char* /*u*/) {
  ensureCapacity_();
  controls_[controlCount_++] = {k, "select", CtrlType::Select, reinterpret_cast<uintptr_t>(&v), 0.0f, 0.0f, (float)v, nullptr, 0, true, false};
  applyPending_(controls_[controlCount_ - 1]);
}

void MoonModule::addControlValue(const char* label) {
  for (int i = (int)controlCount_ - 1; i >= 0; --i) {
    if (controls_[i].type != CtrlType::Select) continue;
    ControlDescriptor& d = controls_[i];
    const char** buf = static_cast<const char**>(std::realloc(d.options, (d.optionCount + 1) * sizeof(const char*)));
    if (!buf) return;
    buf[d.optionCount] = label;
    d.options = buf;
    ++d.optionCount;
    d.maxVal = (float)(d.optionCount - 1);
    return;
  }
}

void MoonModule::addControl(const char* literal, const char* k, const char* u) {
  ensureCapacity_();
  controls_[controlCount_++] = {k, u, CtrlType::String, reinterpret_cast<uintptr_t>(literal), 0.0f, 0.0f, 0.0f, nullptr, 0, false, false};
}

void MoonModule::addControl(char* buf, size_t maxLen, const char* k, const char* u) {
  ensureCapacity_();
  controls_[controlCount_++] = {k, u, CtrlType::EditStr, reinterpret_cast<uintptr_t>(buf), 0.0f, (float)maxLen, 0.0f, nullptr, 0, false, false};
  applyPending_(controls_[controlCount_ - 1]);
}

// -- Control mutation --------------------------------------------------------

bool MoonModule::setControl(const char* key, JsonVariantConst value) {
  for (uint8_t i = 0; i < controlCount_; ++i) {
    if (std::strcmp(controls_[i].key, key) != 0) continue;
    if (controls_[i].type == CtrlType::String) return false;
    if (controls_[i].type == CtrlType::FloatConst) return false;
    if (controls_[i].type == CtrlType::EditStr) {
      const char* s = value.as<const char*>();
      if (s) {
        char* buf = reinterpret_cast<char*>(controls_[i].ptr);
        size_t maxLen = static_cast<size_t>(controls_[i].maxVal);
        std::strncpy(buf, s, maxLen - 1);
        buf[maxLen - 1] = '\0';
      }
      onUpdate(key);
      return true;
    }
    writeThrough_(controls_[i], value.as<float>());
    onUpdate(key);
    return true;
  }
  return false;
}

bool MoonModule::setControl(const char* key, float value) {
  for (uint8_t i = 0; i < controlCount_; ++i) {
    if (std::strcmp(controls_[i].key, key) != 0) continue;
    if (controls_[i].type == CtrlType::String) return false;
    if (controls_[i].type == CtrlType::FloatConst) return false;
    if (controls_[i].type == CtrlType::EditStr) return false;
    writeThrough_(controls_[i], value);
    onUpdate(key);
    return true;
  }
  return false;
}

// -- Schema and value serialization ------------------------------------------

void MoonModule::getSchema(JsonObject out) const {
  out["id"] = id_;
  out["type"] = type_;
  out["name"] = type_;            // v2: name doubles as type until/unless a
                                  // module needs a separate human label
  out["category"] = category();
  // Module-card stats line (frontend reads these to render the heap / class
  // size badges and the green setup-OK dot). psram_size_bytes stays 0 for
  // Sprint 5 — psram-backed allocations land in Sprint 6 with the lighting
  // pipeline. core defaults to 0 (single inline scheduler thread on ESP32).
  out["class_size_bytes"] = (uint32_t)classSize_;
  out["heap_size_bytes"]  = (uint32_t)dynamicMemorySize();
  out["psram_size_bytes"] = 0u;
  out["setup_ok"]         = true;  // Sprint 8 will surface real setup failures
  out["core"]             = (uint32_t)core_;  // Sprint 7: real scheduler affinity
  out["us_per_tick"]      = usPerTick_;       // sampled in runLoop1s; 0 until first 1s window elapses
  JsonArray arr = out["controls"].to<JsonArray>();
  for (uint8_t i = 0; i < controlCount_; ++i) {
    const ControlDescriptor& d = controls_[i];
    JsonObject c = arr.add<JsonObject>();
    c["key"] = d.key;
    c["type"] = d.uiType;
    switch (d.type) {
      case CtrlType::Float:
        c["min"] = d.minVal; c["max"] = d.maxVal; c["default"] = d.defVal;
        c["value"] = *reinterpret_cast<const float*>(d.ptr); break;
      case CtrlType::Uint8:
        c["min"] = d.minVal; c["max"] = d.maxVal; c["default"] = d.defVal;
        c["value"] = *reinterpret_cast<const uint8_t*>(d.ptr); c["integer"] = true; break;
      case CtrlType::Uint16:
        c["min"] = d.minVal; c["max"] = d.maxVal; c["default"] = d.defVal;
        c["value"] = *reinterpret_cast<const uint16_t*>(d.ptr); c["integer"] = true; break;
      case CtrlType::Uint32:
        c["min"] = d.minVal; c["max"] = d.maxVal; c["default"] = d.defVal;
        c["value"] = *reinterpret_cast<const uint32_t*>(d.ptr); c["integer"] = true; break;
      case CtrlType::Bool:
        c["min"] = d.minVal; c["max"] = d.maxVal; c["default"] = d.defVal;
        c["value"] = *reinterpret_cast<const bool*>(d.ptr); break;
      case CtrlType::FloatConst:
        c["min"] = d.minVal; c["max"] = d.maxVal; c["value"] = d.defVal; break;
      case CtrlType::String:
        c["value"] = reinterpret_cast<const char*>(d.ptr);
        break;
      case CtrlType::EditStr:
        c["maxLen"] = (int)d.maxVal;
        c["value"] = reinterpret_cast<const char*>(d.ptr);
        break;
      case CtrlType::Select: {
        c["value"] = *reinterpret_cast<const uint8_t*>(d.ptr);
        c["default"] = (uint8_t)d.defVal;
        JsonArray opts = c["options"].to<JsonArray>();
        for (uint8_t j = 0; j < d.optionCount; ++j) opts.add(d.options[j]);
        break;
      }
    }
  }
}

void MoonModule::getControlValues(JsonObject out) const {
  for (uint8_t i = 0; i < controlCount_; ++i) {
    const ControlDescriptor& d = controls_[i];
    switch (d.type) {
      case CtrlType::Float:      out[d.key] = *reinterpret_cast<const float*>(d.ptr); break;
      case CtrlType::Uint8:      out[d.key] = *reinterpret_cast<const uint8_t*>(d.ptr); break;
      case CtrlType::Uint16:     out[d.key] = *reinterpret_cast<const uint16_t*>(d.ptr); break;
      case CtrlType::Uint32:     out[d.key] = *reinterpret_cast<const uint32_t*>(d.ptr); break;
      case CtrlType::Bool:       out[d.key] = *reinterpret_cast<const bool*>(d.ptr); break;
      case CtrlType::FloatConst: out[d.key] = d.defVal; break;
      case CtrlType::String:     out[d.key] = reinterpret_cast<const char*>(d.ptr); break;
      case CtrlType::EditStr:    out[d.key] = reinterpret_cast<const char*>(d.ptr); break;
      case CtrlType::Select:     out[d.key] = *reinterpret_cast<const uint8_t*>(d.ptr); break;
    }
  }
}

// -- Mid-lifecycle control rebuild -------------------------------------------

void MoonModule::clearControls() {
  uint8_t kept = 0;
  for (uint8_t i = 0; i < controlCount_; ++i) {
    ControlDescriptor& d = controls_[i];
    if (d.system) { controls_[kept++] = d; continue; }
    if (!pendingProps_) pendingProps_ = new JsonDocument();
    switch (d.type) {
      case CtrlType::Float:   (*pendingProps_)[d.key] = *reinterpret_cast<float*>(d.ptr); break;
      case CtrlType::Uint8:   (*pendingProps_)[d.key] = *reinterpret_cast<uint8_t*>(d.ptr); break;
      case CtrlType::Uint16:  (*pendingProps_)[d.key] = *reinterpret_cast<uint16_t*>(d.ptr); break;
      case CtrlType::Select:  (*pendingProps_)[d.key] = *reinterpret_cast<uint8_t*>(d.ptr); break;
      case CtrlType::Uint32:  (*pendingProps_)[d.key] = *reinterpret_cast<uint32_t*>(d.ptr); break;
      case CtrlType::Bool:    (*pendingProps_)[d.key] = *reinterpret_cast<bool*>(d.ptr); break;
      case CtrlType::EditStr: (*pendingProps_)[d.key] = reinterpret_cast<const char*>(d.ptr); break;
      case CtrlType::String:     break;
      case CtrlType::FloatConst: break;
    }
    if (d.ownsOptions && d.options) { std::free(d.options); d.options = nullptr; }
  }
  if (controlCount_ > kept) schemaDirty_ = true;
  controlCount_ = kept;
}

bool MoonModule::hasControl(const char* key) const {
  for (uint8_t i = 0; i < controlCount_; ++i)
    if (std::strcmp(controls_[i].key, key) == 0) return true;
  return false;
}

// -- Child modules -----------------------------------------------------------

void MoonModule::addChild(MoonModule* child) {
  if (childCount_ >= childCapacity_) {
    uint8_t newCap = childCapacity_ + 4;
    MoonModule** buf = new MoonModule*[newCap];
    if (children_) {
      std::memcpy(buf, children_, childCount_ * sizeof(MoonModule*));
      delete[] children_;
    }
    children_ = buf;
    childCapacity_ = newCap;
  }
  children_[childCount_++] = child;
}

void MoonModule::removeChild(MoonModule* child) {
  for (uint8_t i = 0; i < childCount_; ++i) {
    if (children_[i] != child) continue;
    for (uint8_t j = i; j < childCount_ - 1; ++j) children_[j] = children_[j + 1];
    --childCount_;
    return;
  }
}

bool MoonModule::reorderChildren(MoonModule* const* newOrder, uint8_t count) {
  if (count != childCount_) return false;
  std::memcpy(children_, newOrder, count * sizeof(MoonModule*));
  onChildrenReady();
  return true;
}

// -- Private helpers ---------------------------------------------------------

void MoonModule::applyPending_(const ControlDescriptor& d) {
  if (!pendingProps_) return;
  JsonVariantConst v = (*pendingProps_)[d.key];
  if (v.isNull()) return;
  switch (d.type) {
    case CtrlType::Float:   *reinterpret_cast<float*>(d.ptr) = v.as<float>(); break;
    case CtrlType::Uint8:
    case CtrlType::Select:  *reinterpret_cast<uint8_t*>(d.ptr) = v.as<uint8_t>(); break;
    case CtrlType::Uint16:  *reinterpret_cast<uint16_t*>(d.ptr) = v.as<uint16_t>(); break;
    case CtrlType::Uint32:  *reinterpret_cast<uint32_t*>(d.ptr) = v.as<uint32_t>(); break;
    case CtrlType::Bool:    *reinterpret_cast<bool*>(d.ptr) = v.as<bool>(); break;
    case CtrlType::EditStr: {
      const char* s = v.as<const char*>();
      if (s) {
        char* buf = reinterpret_cast<char*>(d.ptr);
        size_t maxLen = static_cast<size_t>(d.maxVal);
        std::strncpy(buf, s, maxLen - 1);
        buf[maxLen - 1] = '\0';
      }
      break;
    }
    case CtrlType::FloatConst: break;
    case CtrlType::String:     break;
  }
}

void MoonModule::freeOwnedOptions_() {
  for (uint8_t i = 0; i < controlCount_; ++i)
    if (controls_[i].ownsOptions && controls_[i].options) std::free(controls_[i].options);
}

void MoonModule::ensureCapacity_() {
  if (controlCount_ < controlCapacity_) return;
  uint8_t newCap = controlCapacity_ + 4;
  ControlDescriptor* buf = new ControlDescriptor[newCap]{};
  if (controls_) {
    std::memcpy(buf, controls_, controlCount_ * sizeof(ControlDescriptor));
    delete[] controls_;
  }
  controls_ = buf;
  controlCapacity_ = newCap;
}

void MoonModule::writeThrough_(const ControlDescriptor& d, float value) {
  switch (d.type) {
    case CtrlType::Float:   *reinterpret_cast<float*>(d.ptr) = value; break;
    case CtrlType::Uint8:
    case CtrlType::Select:  *reinterpret_cast<uint8_t*>(d.ptr) = (uint8_t)value; break;
    case CtrlType::Uint16:  *reinterpret_cast<uint16_t*>(d.ptr) = (uint16_t)value; break;
    case CtrlType::Uint32:  *reinterpret_cast<uint32_t*>(d.ptr) = (uint32_t)value; break;
    case CtrlType::Bool:    *reinterpret_cast<bool*>(d.ptr) = (value != 0.0f); break;
    case CtrlType::FloatConst:
    case CtrlType::String:
    case CtrlType::EditStr: break;  // not writable through this path
  }
}

}  // namespace pmm
