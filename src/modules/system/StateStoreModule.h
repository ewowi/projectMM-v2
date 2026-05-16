#pragma once
//
// StateStoreModule — persists the user's module configuration + per-control
// state to LittleFS (or ./state/ on PC) so the device survives reboots.
//
// On boot (setup()):
//   1. Read /modules.json — array of {type, id} entries the user has added.
//   2. For each entry, ask the ModuleManager to add it (skipping ids that
//      already exist — the four head modules from main.cpp are added before
//      us and we don't want duplicates).
//   3. Each module's runSetup() runs `loadState` from `pendingProps_` already,
//      so we pre-populate that by reading /state/<id>.json before calling
//      `add()`. We do this via `setProps()` (MoonModule contract: stashes
//      props into pendingProps_ which addControl() then drains).
//
// On loop1s: check if the module list ({type,id} array) changed and write
//   /modules.json immediately if so. 1s cadence so a crash shortly after
//   add/remove doesn't lose the structural change.
//
// On loop10s (cheap cadence — flash wear is real):
//   1. For each module, ask it to serialise its state into JSON, compare to
//      the last-written copy. If different, write /state/<id>.json.
//   2. Delete /state/<id>.json files whose ids are no longer in the list.
//
// Why poll-based dirty detection instead of explicit callbacks: the data is
// small (a few hundred bytes per module), the cadence is 10 seconds, the
// comparison is a std::string == — far cheaper than threading a "dirty"
// flag through ModuleManager + HttpServerModule. Add a real callback when
// flash-write latency becomes a measurable problem (it won't at this scale).
//

#include <ArduinoJson.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "../../core/ModuleManager.h"
#include "../../core/MoonModule.h"
#include "../../pal/PalFs.h"
#include "Logger.h"

namespace pmm {

class StateStoreModule : public MoonModule {
 public:
  const char* category() const override { return "system"; }

  void setup() override {
    if (!manager_) return;
    load_modules_from_disk_();
    // Seed the dirty-detection caches with what's on disk now, so the first
    // loop10s pass doesn't immediately rewrite anything we just loaded.
    snapshot_modules_(last_modules_json_);
    snapshot_state_(last_state_json_);
  }

  void loop1s() override {
    if (!manager_) return;
    std::string fresh_modules;
    snapshot_modules_(fresh_modules);
    if (fresh_modules != last_modules_json_) {
      pal::fs_write_text(kModulesPath, fresh_modules);
      log("[state] saved %s (%u bytes)\n", kModulesPath, (unsigned)fresh_modules.size());
      last_modules_json_ = std::move(fresh_modules);
    }
  }

  void loop10s() override {
    if (!manager_) return;
    std::map<std::string, std::string> fresh_state;
    snapshot_state_(fresh_state);

    for (const auto& kv : fresh_state) {
      auto it = last_state_json_.find(kv.first);
      if (it == last_state_json_.end() || it->second != kv.second) {
        const std::string path = std::string(kStatePrefix) +kv.first + ".json";
        pal::fs_write_text(path.c_str(), kv.second);
        log("[state] saved %s (%u bytes)\n", path.c_str(), (unsigned)kv.second.size());
        last_state_json_[kv.first] = kv.second;
      }
    }
    // Reverse pass: drop state files for ids that no longer exist. (We can't
    // unlink in PalFs yet — that lands when a first remover needs it. For
    // Sprint 6 we just leave orphan state files; load_modules_from_disk_
    // ignores them since they're only consulted by id.)
    for (auto it = last_state_json_.begin(); it != last_state_json_.end(); ) {
      if (fresh_state.find(it->first) == fresh_state.end()) it = last_state_json_.erase(it);
      else                                                  ++it;
    }
  }

 private:
  static constexpr const char* kModulesPath  = "/modules.json";
  static constexpr const char* kStatePrefix  = "/state-";  // flat layout: /state-<id>.json
                                                            // (LittleFS doesn't auto-mkdir; flat names sidestep that.
                                                            //  PalFs lands fs_mkdir when something else needs it.)

  std::string last_modules_json_;
  std::map<std::string, std::string> last_state_json_;

  // Serialise the current module list as JSON; serialise each module's state
  // into `state_map`. Lock the manager throughout so the snapshot is consistent.
  void snapshot_modules_(std::string& modules_out) const {
    JsonDocument list;
    JsonArray arr = list.to<JsonArray>();
    std::lock_guard<std::recursive_mutex> lk(manager_->mutex());
    for (size_t i = 0; i < manager_->size(); ++i) {
      MoonModule* m = manager_->at(i);
      if (std::strcmp(m->type(), "state-store") == 0) continue;
      JsonObject e = arr.add<JsonObject>();
      e["type"] = m->type();
      e["id"]   = m->id();
      if (m->parent()) e["parent_id"] = m->parent()->id();
    }
    modules_out.reserve(measureJson(list) + 1);
    serializeJson(list, modules_out);
  }

  void snapshot_state_(std::map<std::string, std::string>& state_out) const {
    std::lock_guard<std::recursive_mutex> lk(manager_->mutex());
    for (size_t i = 0; i < manager_->size(); ++i) {
      MoonModule* m = manager_->at(i);
      if (std::strcmp(m->type(), "state-store") == 0) continue;
      JsonDocument st;
      JsonObject root = st.to<JsonObject>();
      m->saveBaseState(root);
      m->saveState(root);
      std::string body;
      body.reserve(measureJson(st) + 1);
      serializeJson(st, body);
      state_out.emplace(std::string(m->id()), std::move(body));
    }
  }

  void load_modules_from_disk_() {
    const std::string body = pal::fs_read_text(kModulesPath);
    if (body.empty()) return;
    JsonDocument doc;
    if (deserializeJson(doc, body)) {
      log("[state] /modules.json parse failed; ignoring\n");
      return;
    }
    JsonArrayConst arr = doc.as<JsonArrayConst>();
    if (arr.isNull()) return;
    // Pass 1: add all modules (parent may not exist yet).
    int added = 0;
    for (JsonVariantConst entry : arr) {
      const char* type = entry["type"] | "";
      const char* id   = entry["id"]   | "";
      if (!*type || !*id) continue;
      if (manager_->find(id)) continue;  // already there (head modules from main.cpp)
      MoonModule* m = manager_->add(type, id);
      if (!m) { log("[state] unknown type \"%s\" for id \"%s\"\n", type, id); continue; }
      load_state_into_(m);
      ++added;
    }
    // Pass 2: restore parent→child relationships now that all modules exist.
    for (JsonVariantConst entry : arr) {
      const char* id        = entry["id"]        | "";
      const char* parent_id = entry["parent_id"] | "";
      if (!*id || !*parent_id) continue;
      manager_->reparent(id, parent_id);
    }
    log("[state] loaded %d module(s) from %s\n", added, kModulesPath);
  }

  void load_state_into_(MoonModule* m) {
    const std::string path = std::string(kStatePrefix) +m->id() + ".json";
    const std::string body = pal::fs_read_text(path.c_str());
    if (body.empty()) return;
    JsonDocument st;
    if (deserializeJson(st, body)) return;
    JsonObjectConst root = st.as<JsonObjectConst>();
    m->loadBaseState(root);
    m->loadState(root);
    // Apply the loaded values immediately. setControl handles each known
    // key; unknown keys (left from previous module shapes) are ignored.
    for (JsonPairConst kv : root) {
      if (std::strcmp(kv.key().c_str(), "enabled") == 0) continue;  // handled above
      m->setControl(kv.key().c_str(), JsonVariantConst(kv.value()));
    }
  }
};

}  // namespace pmm
