#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "state/state.h"
#include "synth/plugin_kind.h"

namespace eg13 {
class Synth;
}

// Presets: a Performance (both parts) or a Patch (one part) wrapped around the project state JSON.
// Applying one never changes the setup fields (the host-facing wiring of a part and the master
// level, voice gain and latency mode); a Patch also leaves KeyLow/KeyHigh and the master alone.
// Main thread only.
namespace eg13::preset {

enum class Kind { Performance, Patch };

struct Meta {
  Kind kind = Kind::Performance;
  PluginKind plugin = PluginKind::Instrument;
  std::string name, bank, note, credit, licence;
};

constexpr int kFormat = 1;

// MidiChannel, Output, MpeMode, MpeBendRange, CcA..CcD of either part; master Level, VoiceGain,
// LatencyMode. Everything else is sound.
bool isSetupParam(uint32_t id);
// The setup fields plus KeyLow/KeyHigh: what a Patch leaves alone.
bool isPatchExcluded(uint32_t id);

// part -1 saves a Performance, 0 or 1 a Patch of that part (stored as part 0). The kind and the
// plugin written are the part's and the synth's, whatever meta holds. Empty string for a bad part.
std::string save(const Synth& synth, const state::StateStore& store, const Meta& meta, int part);

struct ApplyResult {
  state::LoadResult result = state::LoadResult::Ok;
  std::string message;  // set when result is Invalid
};

// The current state with the preset's sound merged in, to be applied by the caller with
// StateStore::load. A Patch goes into targetPart. Never throws; an empty string (and out.result
// Invalid with a message) if the preset is refused.
std::string mergeForApply(const std::string& currentStateJson, const std::string& presetJson,
                          PluginKind running, int targetPart, ApplyResult& out);

// Nullopt if the text is not a preset this version can read.
std::optional<Meta> readMeta(const std::string& presetJson);

}  // namespace eg13::preset
