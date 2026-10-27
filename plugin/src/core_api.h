#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "eg13/core_api.h"
#include "synth/synth.h"
#include "ui_bridge.h"

namespace eg13 {

// What the C function table needs from the plugin: the synth to read, the bridge to edit through,
// where a resize request goes and how a patch change marks the project dirty (the plugin forwards it to the host's gui extension).
// Main thread only. The context must outlive the table and every editor that holds it.
struct CoreApiContext {
  Synth& synth;
  UiBridge& bridge;
  std::function<void(uint32_t width, uint32_t height)> requestResize;  // may be empty
  std::function<void()> markDirty;                                     // may be empty
  // Presets: the plugin's stateLoad path (store.load, params rescan, restart request; the int is a
  // state::LoadResult) and the current state JSON. A preset is merged into saveState() and applied
  // through loadState. Both may be empty, in which case preset_apply refuses.
  std::function<int(const std::string& stateJson)> loadState;
  std::function<std::string()> saveState;
  // The StateStore's browser string (get / set); may be empty.
  std::function<std::string()> getBrowser;
  std::function<void(const std::string&)> setBrowser;
};

// The function table over ctx. Its `context` points at ctx.
EG13CoreAPI makeCoreApi(CoreApiContext& ctx);

// The function table's factory-preset members, which makeCoreApi leaves empty (no presets): the
// factory table needs the preset tool, which needs makeCoreApi. Linked as eg13_core_api_factory.
void attachFactoryPresets(EG13CoreAPI& api);

// Merges a preset file into save() and loads the result: preset_apply without the dirty mark.
// Returns an EG13_PRESET_ value; message is set when it is EG13_PRESET_INVALID.
int32_t applyPresetJson(PluginKind kind, const std::function<std::string()>& save,
                        const std::function<int(const std::string&)>& load, const std::string& json,
                        int targetPart, std::string& message);

// New instrument instances open on the factory preset marked firstLoad, applied like preset_apply
// (merge into save(), then load); EG13 FX, and an instrument with no such preset, keep the init
// state. True if a preset was applied.
bool applyFirstLoadPreset(PluginKind kind, const std::function<std::string()>& save,
                          const std::function<int(const std::string&)>& load);

}  // namespace eg13
