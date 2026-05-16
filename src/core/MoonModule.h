#pragma once
//
// MoonModule — the runtime contract for projectMM v2.
//
// One class merges what v1 split into Module (the lifecycle base) and
// StatefulModule (the controls + persistence + children layer). The merge
// follows from system.md's MoonModule section: every real module ends up
// using controls, so the cost of carrying the system is byte-level (a
// lazily-grown vector) and the architectural simplification removes a
// concept boundary that paid for nothing.
//
// Port-and-minimize log (§4 deliberation, vs v1's StatefulModule.h):
//   KEPT verbatim or with minor renames (future-needed in Release 1):
//     - ControlDescriptor + CtrlType (control schema descriptors)
//     - addControl overloads (float, uint8, uint32, bool, char*, const char*,
//       editable string buffer, float&& display, select dropdown,
//       dynamic-options select) — Sprint 3 + 5 + 6 modules use these
//     - setControl (JSON + float overloads), onUpdate, controlAllocBytes virtual
//     - getSchema, getControlValues (Sprint 3 WS frames; Sprint 5 REST)
//     - pendingProps_, applyPending_ (Sprint 7 state persistence)
//     - setProps, loadState, saveState, saveBaseState, loadBaseState (Sprint 7)
//     - AutoWireSpec + autoWireKeys + setInput hooks (Sprint 5)
//     - fillSystemJson (Sprint 5 /api/system contribution)
//     - addChild / removeChild / reorderChildren (Sprint 6 effect trees)
//     - runSetup/runLoop/runTeardown/runLoop20ms/runLoop1s recursion
//     - clearControls / schemaDirty_
//
//   ADDED back without CRTP (footprint reporting v1 got via the template wrapper):
//     - classSize() reads classSize_, injected ONCE at registration time by
//       ModuleManager::register_type<T>(). Modules need zero per-class
//       boilerplate; no CRTP; no macro.
//     - dynamicMemorySize() = moduleAllocBytes_ + controls/children
//       overhead. Module sets moduleAllocBytes_ inside its onAllocateMemory
//       override; single source of truth.
//     - PSRAM/heap not split: PSRAM is used by default when present;
//       reporters care about the total. Split can return if Sprint 4+ has
//       a concrete consumer that needs it.
//
//   GENERALIZED v1's onSizeChanged + rebuildControls into two on*() hooks:
//     - onAllocateMemory(): single no-args hook for dynamic-memory
//       allocation. Module override sets moduleAllocBytes_ to the bytes
//       it allocated. Framework calls it at runSetup() (after children
//       settle); external code calls it directly to re-trigger.
//     - onBuildControls(): module puts ALL addControl() calls here, not
//       in setup(). Framework calls it at runSetup(); external code calls
//       it directly to rebuild the schema. Modules supporting rebuild
//       start with clearControls() (safe no-op on first call, necessary
//       on re-trigger). No rebuild*() wrappers — the on*() methods are
//       the single public entry points.
//
//   DEFERRED to Sprint 6 (light-domain concepts that don't belong in the
//   universal contract — HttpServerModule.dim() has no meaning):
//     - DIM_ANY / DIM_1D / DIM_2D / DIM_3D + dim() virtual
//     - pixelBuf, physicalMap, snapshot, snapshotRaw — RGB + geometry
//     - PhysMap forward declaration
//
//   DEFERRED to the sprint that wires its dependency:
//     - MemBoot/MemLive printf logging in runSetup() — Sprint 4 when
//       pal::free_heap_bytes / max_alloc_bytes / free_psram_bytes land
//     - pal::check_alloc() heap-safety in setControl() — Sprint 4 same
//     - setupOk_ / disableSelf() — Sprint 4
//     - setupHeapDelta_ + accessor — Sprint 4
//     - ModuleTiming + selfMsPerTick / msPerTick — Timing.h not ported
//     - LOG_SETUP / LOG_HEALTH — Logger.h not ported
//     - meta() JsonDocument helper — debugging convenience
//
//   DROPPED (architectural decision, not a re-port target):
//     - CRTP StatefulModule<Derived> wrapper — v2 collapses to one class
//     - explicit heapSize() / psramSize() / baseHeapUsage() — consolidated
//       into dynamicMemorySize() with moduleAllocBytes_ as the cache
//     - rebuildMemory() / rebuildControls() — consolidated into the on*()
//       methods themselves; modules pay one line of bookkeeping inside
//       the override instead of two methods per concern in the API
//
// No patches-over-symptoms were found per §3 ("strip patches, not features").
//
// Implementations of non-trivial methods live in MoonModule.cpp; this header
// is declarations + small inline accessors. Modules including MoonModule.h
// stop re-parsing the full implementation on every translation unit.
//

#include <ArduinoJson.h>
#include <cstdint>
#include <cstring>
#include <string>

namespace pmm {

class ModuleManager;

// One registered control entry. Stores a typed pointer to a backing class
// field so setControl() writes through it without per-tick JSON overhead.
enum class CtrlType : uint8_t { Float, Uint8, Uint16, Uint32, Bool, String, EditStr, FloatConst, Select };

struct ControlDescriptor {
  const char* key;       // JSON key used by REST / WS and as UI label
  const char* uiType;    // "slider", "color", "select", "display", "text", "toggle", "progress"
  CtrlType type;
  uintptr_t ptr;         // raw address of backing field (must outlive descriptor)
  float minVal;
  float maxVal;          // EditStr: buffer capacity (maxLen); Select: optionCount-1
  float defVal;          // snapshot at addControl() time (unused for String/EditStr)
  const char** options;  // Select-only: option labels
  uint8_t optionCount;
  bool ownsOptions;      // Select: heap-allocated options array, free on clear
  bool system = false;   // survives clearControls() (e.g. enabled)
};

// One rule in a module's automatic input-wiring declaration. Sprint 5 uses
// this to wire WiFi → HTTP, etc. without type-specific strcmp chains.
struct AutoWireSpec {
  const char* inputKey;    // my input key; nullptr terminates the array
  const char* searchType;  // type name to find among owned modules
  bool allMatches;         // wire all matches vs first only
  const char* backKey;     // non-null: also call found->setInput(backKey, this)
};

class MoonModule {
 public:
  MoonModule()                             = default;
  virtual ~MoonModule();
  MoonModule(const MoonModule&)            = delete;
  MoonModule& operator=(const MoonModule&) = delete;

  // -- Lifecycle hooks (override in concrete modules) ------------------------
  virtual void setup() {}
  virtual void loop() {}
  virtual void loop20ms() {}
  virtual void loop1s() {}
  virtual void loop10s() {}
  virtual void teardown() {}

  // Register all addControl() calls here. Called automatically at runSetup()
  // time (after setup() has configured non-control state), and externally
  // when something needs the schema rebuilt at runtime. Modules supporting
  // rebuild start with clearControls() (safe no-op on first call; preserves
  // enabled_ on re-trigger). Framework adds the enabled_ toggle at index 0
  // after this returns on the initial runSetup pass.
  //
  //   void onBuildControls() override {
  //     clearControls();
  //     addControl(speed_, "speed", "slider", 0, 100);
  //   }
  virtual void onBuildControls() {}

  // Called after all children have finished their own setup(); override in
  // parent modules that need to react to children's declared shapes.
  virtual void onChildrenReady() {}

  // Allocate dynamic memory based on current state. Called automatically at
  // the end of runSetup() and externally on reallocation triggers. Override
  // and set moduleAllocBytes_ to the bytes you allocated — that one line is
  // the single source of truth for dynamicMemorySize().
  //
  //   void onAllocateMemory() override {
  //     delete[] buf_; buf_ = new uint8_t[size_];
  //     moduleAllocBytes_ = size_;
  //   }
  virtual void onAllocateMemory() {}

  // Override to recompute derived values after a control changes.
  virtual void onUpdate(const char* /*key*/) {}

  // Sprint 4 ESP32 heap-safety: return bytes the change would allocate;
  // setControl will call pal::check_alloc before applying.
  virtual size_t controlAllocBytes(const char* /*key*/) const { return 0; }

  // -- Dispatch wrappers (Scheduler calls these; recurse into children) ------
  virtual void runSetup();
  virtual void runLoop();
  virtual void runLoop20ms();
  virtual void runLoop1s();
  virtual void runLoop10s();
  virtual void runTeardown();

  // -- Identity --------------------------------------------------------------
  const char* id() const { return id_.c_str(); }
  const char* type() const { return type_; }
  ModuleManager* manager() const { return manager_; }
  // Override per leaf class — used by the frontend for grouping/labelling.
  virtual const char* category() const { return "system"; }
  bool isEnabled() const { return enabled_; }
  void setEnabled(bool e) { enabled_ = e; }

  // Hand-rolled fallback JSON line (Sprint 2 contract: ModuleManager and tests
  // use this when there are no controls). Modules with controls generally use
  // getSchema()/getControlValues() instead.
  virtual void serialize_json(std::string& out) const {
    out += "{\"type\":\"";
    out += type_;
    out += "\",\"id\":\"";
    out += id_;
    out += "\"}";
  }

  // -- State persistence (Sprint 7 modules.json + per-id state files) -------
  // Default: stash props into pendingProps_ so addControl() can apply them.
  virtual void setProps(JsonObjectConst props);
  // Wire a named data-flow input to another module.
  virtual void setInput(const char* /*key*/, MoonModule* /*source*/) {}
  // Default: merge saved state into pendingProps_ (overwrites stashed props).
  virtual void loadState(JsonObjectConst state);
  // Default: write all registered control values to persistent state.
  virtual void saveState(JsonObject state) const;
  // Persist enabled_ separately so concrete modules don't need to know about it.
  void saveBaseState(JsonObject state) const { if (!enabled_) state["enabled"] = false; }
  void loadBaseState(JsonObjectConst state) {
    if (!state["enabled"].isNull()) enabled_ = (bool)state["enabled"];
  }

  // -- Control registration --------------------------------------------------
  // Convention: call addControl() for a field inside onBuildControls() so
  // pending props/state can apply before the first read.
  void addControl(float& v, const char* k, const char* u, float lo = 0.0f, float hi = 1.0f);
  void addControl(uint8_t&  v, const char* k, const char* u, uint8_t  lo = 0,  uint8_t  hi = 255);
  void addControl(uint16_t& v, const char* k, const char* u, uint16_t lo = 0,  uint16_t hi = 65535);
  void addControl(uint32_t& v, const char* k, const char* u, uint32_t lo = 0,  uint32_t hi = 0);
  void addControl(bool& v, const char* k, const char* u, float lo = 0.0f, float hi = 1.0f);
  // Read-only string DISPLAY backed by a char[] field (e.g. localTime_).
  void addControl(char* v, const char* k, const char* u);
  // Static display whose value is baked in at setup() time.
  void addControl(float&&    value, const char* k, const char* u, float    lo = 0.0f, float    hi = 0.0f);
  void addControl(int8_t&&   value, const char* k, const char* u, int8_t   lo = -128, int8_t  hi = 127);
  void addControl(uint8_t&&  value, const char* k, const char* u, uint8_t  lo = 0,    uint8_t  hi = 255);
  void addControl(uint16_t&& value, const char* k, const char* u, uint16_t lo = 0,    uint16_t hi = 65535);
  void addControl(uint32_t&& value, const char* k, const char* u, uint32_t lo = 0,    uint32_t hi = 0);
  // Select (dropdown) backed by a uint8_t index field; options must outlive module.
  void addControl(uint8_t& v, const char* k, const char* const* opts, uint8_t count);
  // Select with dynamic options populated via addControlValue() — options
  // pointer array is heap-allocated, freed on clear.
  void addControl(uint8_t& v, const char* k, const char* /*u*/);
  // Append a label to the most recently registered select control.
  void addControlValue(const char* label);
  // Read-only string display backed by a stable const char* (literal/static).
  void addControl(const char* literal, const char* k, const char* u);
  // Editable string backed by a char[] buffer of capacity maxLen (incl null).
  void addControl(char* buf, size_t maxLen, const char* k, const char* u);

  // -- Control mutation ------------------------------------------------------
  // Write a control via JSON variant (REST / WS handler path). Calls
  // onUpdate(key) after the field is written. Returns true if key found.
  bool setControl(const char* key, JsonVariantConst value);
  // Convenience overload for float — avoids JsonDocument at the call site.
  bool setControl(const char* key, float value);

  // -- Schema and value serialization (Sprint 3 WS + REST) ------------------
  void getSchema(JsonObject out) const;
  // Flat {key: value} JSON — lighter alternative for periodic WS state pushes.
  void getControlValues(JsonObject out) const;


  // -- Mid-lifecycle control rebuild -----------------------------------------
  // Saves non-system control values back into pendingProps_ so addControl()
  // restores them, preserves system controls (enabled), sets schemaDirty_.
  // Modules supporting rebuild call this at the start of onBuildControls().
  void clearControls();
  bool schemaDirty() const { return schemaDirty_; }
  void clearSchemaDirty() { schemaDirty_ = false; }
  uint8_t controlCount() const { return controlCount_; }
  bool hasControl(const char* key) const;

  // -- Auto-wiring (Sprint 5) ------------------------------------------------
  virtual const AutoWireSpec* autoWireKeys() const { return nullptr; }
  virtual const char* moduleBaseType() const { return "MoonModule"; }

  // -- Core affinity (Sprint 7) ---------------------------------------------
  // Which scheduler core (0 = PRO_CPU, 1 = APP_CPU on ESP32; logical id on
  // PC) ticks this module. Concrete modules set their core in the
  // constructor: `RipplesEffect()` keeps the default 0; `ArtnetOutModule()`
  // overrides to 1 so UDP send work runs off the main hot path. Settable
  // at runtime later when a UI control wants to remap.
  uint8_t coreAffinity() const     { return core_; }
  void    setCoreAffinity(uint8_t c) { core_ = c; }

  // -- Footprint reporting ---------------------------------------------------
  size_t classSize() const { return classSize_; }
  void   setClassSize(size_t s) { classSize_ = (uint16_t)s; }
  // Total dynamic memory: module's own (moduleAllocBytes_, set by its
  // onAllocateMemory override) + framework overhead (controls + children).
  size_t dynamicMemorySize() const {
    return moduleAllocBytes_
         + controlCapacity_ * sizeof(ControlDescriptor)
         + childCapacity_   * sizeof(MoonModule*);
  }

  // -- System info JSON (Sprint 5 /api/system) ------------------------------
  // Only SystemStatusModule is expected to override this.
  virtual void fillSystemJson(JsonObject /*out*/) const {}

  // -- Child modules (Sprint 6 effect trees) --------------------------------
  // MoonModule does NOT own children — ModuleManager holds unique_ptrs.
  // Children array is lazily allocated; childless modules pay only 6 bytes.
  void addChild(MoonModule* child);
  void removeChild(MoonModule* child);
  bool reorderChildren(MoonModule* const* newOrder, uint8_t count);
  uint8_t childCount() const { return childCount_; }
  MoonModule* child(uint8_t i) const { return i < childCount_ ? children_[i] : nullptr; }

 protected:
  // Field order is optimised for minimum padding on 64-bit:
  // 8B blocks → 4B → 2B → 1B, eliminating 24 B of alignment waste vs. naïve order.
  std::string id_;                  // 24B — owned, runtime-assigned instance name
  const char* type_       = nullptr;// 8B  — points into factory map key (stable)
  ModuleManager* manager_ = nullptr;// 8B
  ControlDescriptor* controls_ = nullptr; // 8B — lazily grown
  MoonModule** children_  = nullptr;// 8B  — lazily grown; owned by ModuleManager
  // Heap-allocated only when setProps()/loadState() is called; freed after
  // runSetup() drains it. Null pointer = no pending values (common case).
  JsonDocument* pendingProps_ = nullptr; // 8B

  // Tick counter incremented from runLoop(); sampled in runLoop1s() to derive
  // usPerTick (and from it fps) for the per-module timing line in the UI.
  uint32_t tickCount_        = 0;
  uint32_t timingPrevTicks_  = 0;
  uint32_t timingPrevMs_     = 0;
  uint32_t moduleAllocBytes_ = 0;   // set by the module inside onAllocateMemory()

  uint16_t classSize_  = 0;         // set by ModuleManager::register_type<T>; max ~600B fits uint16_t
  uint16_t usPerTick_  = 0;         // µs/tick; max 65535 µs = 65 ms
 public:
  uint16_t usPerTick() const { return usPerTick_; }
 protected:

  bool    enabled_         = true;
  bool    schemaDirty_     = false;
  uint8_t core_            = 0;     // scheduler core affinity
  uint8_t controlCount_    = 0;
  uint8_t controlCapacity_ = 0;
  uint8_t childCount_      = 0;
  uint8_t childCapacity_   = 0;

  friend class ModuleManager;

 private:
  void applyPending_(const ControlDescriptor& d);
  void freeOwnedOptions_();
  void ensureCapacity_();
  static void writeThrough_(const ControlDescriptor& d, float value);
};

}  // namespace pmm
