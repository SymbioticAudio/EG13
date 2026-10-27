#include "preset/preset.h"

#include <string>

#include <nlohmann/json.hpp>

#include "params/param_ids.h"
#include "synth/param_table.h"
#include "synth/synth.h"

namespace eg13::preset {
namespace {

using json = nlohmann::json;

constexpr const char* kSectionKeys[] = {"engineSettings", "filterSettings", "shaperSettings",
                                        "mod1Settings",   "mod2Settings",   "mod3Settings",
                                        "mod4Settings",   "partFxSettings"};

uint32_t partOf(uint32_t id) { return id >> 16; }

bool partSetup(uint32_t index) {
  switch (PartParam(index)) {
    case PartParam::MidiChannel:
    case PartParam::Output:
    case PartParam::MpeMode:
    case PartParam::MpeBendRange:
    case PartParam::CcA:
    case PartParam::CcB:
    case PartParam::CcC:
    case PartParam::CcD:
      return true;
    default:
      return false;
  }
}

bool parseId(const std::string& text, uint32_t* id) {
  if (text.empty() || text.size() > 10) return false;
  uint64_t v = 0;
  for (char c : text) {
    if (c < '0' || c > '9') return false;
    v = v * 10 + uint64_t(c - '0');
  }
  if (v > 0xFFFFFFFFull) return false;
  *id = uint32_t(v);
  return true;
}

const char* kindName(Kind k) { return k == Kind::Patch ? "patch" : "performance"; }
const char* pluginName(PluginKind k) { return k == PluginKind::Effect ? "effect" : "instrument"; }

std::string str(const json& j, const char* key) {
  const auto it = j.find(key);
  return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

// A readable preset file: its meta and the state object. Sets message when it is refused.
struct Parsed {
  Meta meta;
  json state;
};

bool parsePreset(const std::string& text, Parsed& out, std::string& message) {
  json root = json::parse(text, nullptr, false);
  if (!root.is_object()) {
    message = "This is not an EG13 preset.";
    return false;
  }
  const auto fmt = root.find("eg13Preset");
  if (fmt == root.end() || !fmt->is_number_integer()) {
    message = "This is not an EG13 preset.";
    return false;
  }
  const long long version = fmt->get<long long>();
  if (version > kFormat) {
    message = "This preset uses a newer preset format (" + std::to_string(version) +
              "); update EG13 to open it.";
    return false;
  }
  if (version < 1) {
    message = "This is not an EG13 preset.";
    return false;
  }
  const std::string kind = str(root, "kind");
  const std::string plugin = str(root, "plugin");
  if ((kind != "performance" && kind != "patch") || (plugin != "instrument" && plugin != "effect")) {
    message = "This preset has an unknown kind or plugin.";
    return false;
  }
  const auto state = root.find("state");
  if (state == root.end() || !state->is_object() || !state->contains("params") ||
      !(*state)["params"].is_object()) {
    message = "This preset has no usable sound data.";
    return false;
  }
  out.meta.kind = kind == "patch" ? Kind::Patch : Kind::Performance;
  out.meta.plugin = plugin == "effect" ? PluginKind::Effect : PluginKind::Instrument;
  out.meta.name = str(root, "name");
  out.meta.bank = str(root, "bank");
  out.meta.note = str(root, "note");
  out.meta.credit = str(root, "credit");
  out.meta.licence = str(root, "licence");
  out.state = *state;
  return true;
}

// Copies json[key][from] over json[key][to] (a part's entry) when src has it.
void copyPartEntry(const json& src, json& dst, const char* key, const std::string& from, const std::string& to) {
  const auto it = src.find(key);
  if (it == src.end() || !it->is_object()) return;
  const auto entry = it->find(from);
  if (entry == it->end()) return;
  if (!dst.contains(key) || !dst[key].is_object()) dst[key] = json::object();
  dst[key][to] = *entry;
}

void copyPartEntries(const json& src, json& dst, const std::string& from, const std::string& to) {
  for (const char* key : kSectionKeys) copyPartEntry(src, dst, key, from, to);
  copyPartEntry(src, dst, "routes", from, to);
  copyPartEntry(src, dst, "envelopeShapes", from, to);
}

}  // namespace

bool isSetupParam(uint32_t id) {
  const uint32_t part = partOf(id);
  const uint32_t slot = (id >> 8) & 0xFF;
  const uint32_t index = id & 0xFF;
  if (slot != uint32_t(params::Slot::Part)) return false;
  if (part == params::kMasterPart) {
    return index == uint32_t(MasterParam::Level) || index == uint32_t(MasterParam::VoiceGain) ||
           index == uint32_t(MasterParam::LatencyMode);
  }
  return part < uint32_t(kNumParts) && partSetup(index);
}

bool isPatchExcluded(uint32_t id) {
  if (isSetupParam(id)) return true;
  const uint32_t slot = (id >> 8) & 0xFF;
  const uint32_t index = id & 0xFF;
  return partOf(id) < uint32_t(kNumParts) && slot == uint32_t(params::Slot::Part) &&
         (index == uint32_t(PartParam::KeyLow) || index == uint32_t(PartParam::KeyHigh));
}

std::string save(const Synth& synth, const state::StateStore& store, const Meta& meta, int part) {
  if (part < -1 || part >= kNumParts) return {};
  const json full = json::parse(store.save(synth), nullptr, false);
  if (!full.is_object() || !full.contains("params")) return {};
  const bool patch = part >= 0;

  json state = json::object();
  state["schemaVersion"] = state::kSchemaVersion;
  json outParams = json::object();
  for (auto it = full["params"].begin(); it != full["params"].end(); ++it) {
    uint32_t id = 0;
    if (!parseId(it.key(), &id) || paramIndex(id) < 0) continue;
    if (patch ? (partOf(id) != uint32_t(part) || isPatchExcluded(id)) : isSetupParam(id)) continue;
    // A Patch keeps its part as part 0.
    outParams[std::to_string(patch ? (id & 0xFFFF) : id)] = it.value();
  }
  state["params"] = std::move(outParams);
  if (patch) {
    copyPartEntries(full, state, std::to_string(part), "0");
  } else {
    for (int p = 0; p < kNumParts; ++p) copyPartEntries(full, state, std::to_string(p), std::to_string(p));
  }

  json root = json::object();
  root["eg13Preset"] = kFormat;
  root["kind"] = kindName(patch ? Kind::Patch : Kind::Performance);
  root["plugin"] = pluginName(synth.kind());
  root["name"] = meta.name;
  root["bank"] = meta.bank;
  root["note"] = meta.note;
  root["credit"] = meta.credit;
  root["licence"] = meta.licence;
  root["state"] = std::move(state);
  return root.dump(2);
}

std::string mergeForApply(const std::string& currentStateJson, const std::string& presetJson,
                          PluginKind running, int targetPart, ApplyResult& out) {
  out = ApplyResult{};
  const auto refuse = [&out](const std::string& message) {
    out.result = state::LoadResult::Invalid;
    out.message = message;
    return std::string();
  };
  try {
    Parsed preset;
    std::string message;
    if (!parsePreset(presetJson, preset, message)) return refuse(message);
    if (preset.meta.plugin != running) {
      return refuse(running == PluginKind::Instrument
                        ? "This preset is for EG13 FX; open it in EG13 FX."
                        : "This preset is for the EG13 instrument, not EG13 FX.");
    }
    const bool patch = preset.meta.kind == Kind::Patch;
    if (patch && (targetPart < 0 || targetPart >= kNumParts)) return refuse("There is no such part to load into.");

    json current = json::parse(currentStateJson, nullptr, false);
    if (!current.is_object() || !current.contains("params") || !current["params"].is_object()) {
      return refuse("The current state could not be read.");
    }

    for (auto it = preset.state["params"].begin(); it != preset.state["params"].end(); ++it) {
      uint32_t id = 0;
      if (!it.value().is_number() || !parseId(it.key(), &id) || paramIndex(id) < 0) continue;
      if (patch) {
        if (partOf(id) != 0 || isPatchExcluded(id)) continue;
        id |= uint32_t(targetPart) << 16;
      } else if (isSetupParam(id)) {
        continue;
      }
      current["params"][std::to_string(id)] = it.value();
    }
    if (patch) {
      copyPartEntries(preset.state, current, "0", std::to_string(targetPart));
    } else {
      for (int p = 0; p < kNumParts; ++p) copyPartEntries(preset.state, current, std::to_string(p), std::to_string(p));
    }
    return current.dump();
  } catch (const std::exception& e) {
    return refuse(std::string("This preset could not be read: ") + e.what());
  } catch (...) {
    return refuse("This preset could not be read.");
  }
}

std::optional<Meta> readMeta(const std::string& presetJson) {
  try {
    Parsed preset;
    std::string message;
    if (!parsePreset(presetJson, preset, message)) return std::nullopt;
    return preset.meta;
  } catch (...) {
    return std::nullopt;
  }
}

}  // namespace eg13::preset
