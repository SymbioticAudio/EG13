#include "state/state.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "engine/engine_registry.h"
#include "synth/param_table.h"
#include "synth/synth.h"

namespace eg13::state {

namespace {

bool parseId(const std::string& key, uint32_t* id) {
  if (key.empty() || key.size() > 10) return false;
  for (char c : key) {
    if (c < '0' || c > '9') return false;
  }
  const unsigned long long value = std::strtoull(key.c_str(), nullptr, 10);
  if (value > 0xFFFFFFFFull) return false;
  *id = uint32_t(value);
  return true;
}

// Schema 1 (M0) -> 2: the nine M0 parameters kept their IDs and ranges, so no
// transformation is needed. Schema 2 -> 3 (M2): Source engine selector. Schema 3 -> 4 (M3):
// Filter and Shaper types, Off for older documents. Schema 4 -> 5 (M4): mod slots, routes and
// envelope shapes; older documents load with no modulation at their old level (Voice Gain 0 dB,
// Soft Clip off; provisional ruling 30).

// Source engineSettings arrays hold kSourceParamCount numbers. Early unreleased schema 3 builds
// wrote 8 (P1..P8); those load as a prefix, the rest keep the engine's defaults.
constexpr size_t kLegacyEngineSettingsSize = 8;

// Every slot's per-engine settings: {part: {engine: [P values]}}.
struct Section {
  SlotKind kind;
  const char* key;
};
const Section kSections[kNumSlotKinds] = {
    {SlotKind::Source, "engineSettings"},  // M2's key
    {SlotKind::Filter, "filterSettings"},
    {SlotKind::Shaper, "shaperSettings"},
    {SlotKind::Mod1, "mod1Settings"},  // M4: P1..P16, then the Scope
    {SlotKind::Mod2, "mod2Settings"},
    {SlotKind::Mod3, "mod3Settings"},
    {SlotKind::Mod4, "mod4Settings"},
};

// Numbers per engine in a section: the P's, and for a mod slot its Scope last.
size_t sectionWidth(SlotKind kind) { return slotParamCount(kind) + (isModSlot(kind) ? 1 : 0); }

bool isKnownTopLevel(const std::string& key) {
  if (key == "schemaVersion" || key == "params" || key == "routes" || key == "envelopeShapes") return true;
  for (const Section& s : kSections) {
    if (key == s.key) return true;
  }
  return false;
}

// Reads one section into the stash. False if anything had to be dropped.
bool readSection(const nlohmann::json& root, const Section& section, EngineStash& stash) {
  const auto found = root.find(section.key);
  if (found == root.end()) return true;
  if (!found->is_object()) return false;
  bool clean = true;
  const size_t count = sectionWidth(section.kind);
  for (auto partIt = found->begin(); partIt != found->end(); ++partIt) {
    uint32_t part = 0;
    if (!parseId(partIt.key(), &part) || part >= uint32_t(kNumParts) || !partIt.value().is_object()) {
      clean = false;
      continue;
    }
    for (auto engineIt = partIt.value().begin(); engineIt != partIt.value().end(); ++engineIt) {
      uint32_t engine = 0;
      const auto& list = engineIt.value();
      bool valid = parseId(engineIt.key(), &engine) && engine < uint32_t(slotEngineCount(section.kind)) &&
                   list.is_array() &&
                   (list.size() == count ||
                    (section.kind == SlotKind::Source && list.size() == kLegacyEngineSettingsSize));
      for (size_t p = 0; valid && p < list.size(); ++p) valid = list[p].is_number();
      if (!valid) {
        clean = false;
        continue;
      }
      for (size_t p = 0; p < list.size(); ++p) {
        const double v = list[p].get<double>();
        const float clamped = float(std::clamp(std::isnan(v) ? 0.0 : v, 0.0, 1.0));
        if (p < slotParamCount(section.kind)) {
          stash[slotStashIndex(section.kind, int(part), int(engine), int(p))] = clamped;
        } else {  // a mod slot's Scope: 0 per voice, 1 per part
          stash[scopeStashIndex(int(part), modSlotIndex(section.kind), int(engine))] = std::round(clamped);
        }
      }
    }
  }
  return clean;
}

// One route entry: [source, destination, via], integers 0..65535. A newer version's entry may
// carry more numbers after these three: they are not read here (see hasExtras).
bool parseRoute(const nlohmann::json& entry, Route* out) {
  if (!entry.is_array() || entry.size() < 3) return false;
  uint16_t code[3] = {};
  for (size_t k = 0; k < 3; ++k) {
    if (!entry[k].is_number_integer()) return false;
    const long long v = entry[k].get<long long>();
    if (v < 0 || v > 65535) return false;
    code[k] = uint16_t(v);
  }
  *out = {code[0], code[1], code[2]};
  return true;
}

// One envelope shape: {"count": n, "types": [6], "loops": [6]}, sanitized. A newer version's
// shape may have more segments or keys: only the first six segments are read.
bool parseShape(const nlohmann::json& e, EnvelopeShape* out) {
  if (!e.is_object()) return false;
  constexpr size_t kSegments = size_t(EnvelopeShape::kMaxSegments);
  const auto count = e.find("count");
  const auto types = e.find("types");
  const auto loops = e.find("loops");
  if (count == e.end() || !count->is_number_integer() || types == e.end() || !types->is_array() ||
      types->size() < kSegments || loops == e.end() || !loops->is_array() || loops->size() < kSegments) {
    return false;
  }
  for (size_t i = 0; i < kSegments; ++i) {
    if (!(*types)[i].is_number_integer() || !(*loops)[i].is_number_integer()) return false;
  }
  EnvelopeShape shape;
  shape.count = uint8_t(std::clamp(count->get<long long>(), 0LL, 255LL));
  for (size_t i = 0; i < kSegments; ++i) {
    shape.type[i] = uint8_t(std::clamp((*types)[i].get<long long>(), 0LL, 255LL));
    shape.loop[i] = uint8_t(std::clamp((*loops)[i].get<long long>(), 0LL, 255LL));
  }
  *out = sanitized(shape);
  return true;
}

// Whether a parsed entry holds more than this version reads (a newer version's data).
bool hasExtras(const nlohmann::json& entry, const Route*) { return entry.size() > 3; }
bool hasExtras(const nlohmann::json& entry, const EnvelopeShape*) {
  const size_t kSegments = size_t(EnvelopeShape::kMaxSegments);
  return entry.size() > 3 || entry["types"].size() > kSegments || entry["loops"].size() > kSegments;
}

// "routes": {part: [[source, destination, via], ... 12]} and "envelopeShapes": {part: [{"count":
// n, "types": [6], "loops": [6]}, ... 4]}. Codes are kept as saved (unknown ones too: they are
// ignored, and come back unchanged on resave); out-of-range shape values are sanitized. Entries
// that cannot be read are dropped (the default: no route, an ADSR). A part named twice ("0" and
// "00") is read once, from the first key in key order. More than this version holds (parts,
// entries, numbers in an entry, segments; a newer version's data) is read as far as it goes and
// written back on save (mergeExtras). False if anything was dropped, or if a document of this
// version or older holds more than it should.
template <typename T, size_t N>
bool readPatchSection(const nlohmann::json& root, const char* key, bool newer,
                      std::array<std::array<T, N>, kNumParts>& out, bool (*parse)(const nlohmann::json&, T*)) {
  const auto found = root.find(key);
  if (found == root.end()) return true;
  if (!found->is_object()) return false;
  bool clean = true;
  bool seen[kNumParts] = {};
  for (auto partIt = found->begin(); partIt != found->end(); ++partIt) {
    uint32_t part = 0;
    const auto& list = partIt.value();
    if (!parseId(partIt.key(), &part)) {
      clean = false;
      continue;
    }
    if (part >= uint32_t(kNumParts)) {  // a part this version does not have: kept for the resave
      if (!newer) clean = false;
      continue;
    }
    if (seen[part] || !list.is_array()) {
      clean = false;
      continue;
    }
    seen[part] = true;
    if (list.size() > N && !newer) clean = false;
    for (size_t i = 0; i < std::min(list.size(), N); ++i) {
      if (!parse(list[i], &out[part][i])) {
        clean = false;
      } else if (!newer && hasExtras(list[i], static_cast<const T*>(nullptr))) {
        clean = false;
      }
    }
  }
  return clean;
}

// The "routes" or "envelopeShapes" this version writes, merged with what it loaded: parts this
// version does not have are kept whole; in each of its parts, entries past this version's count
// are appended, and an entry it read is written back as loaded while it still reads the same, so
// an edit replaces it. Entries that could not be read were dropped at load and stay dropped.
template <typename T, size_t N>
void mergeExtras(nlohmann::json& ours, const std::string& loadedDump, const std::array<std::array<T, N>, kNumParts>& now,
                 bool (*parse)(const nlohmann::json&, T*)) {
  if (loadedDump.empty()) return;
  const nlohmann::json loaded = nlohmann::json::parse(loadedDump, nullptr, false);
  if (!loaded.is_object()) return;
  for (auto it = loaded.begin(); it != loaded.end(); ++it) {
    uint32_t part = 0;
    if (!parseId(it.key(), &part)) continue;
    if (part >= uint32_t(kNumParts)) {
      ours[it.key()] = it.value();
      continue;
    }
    if (it.key() != std::to_string(part) || !it.value().is_array()) continue;  // read as `part`, or dropped
    nlohmann::json& list = ours[it.key()];
    const auto& saved = it.value();
    for (size_t i = 0; i < saved.size(); ++i) {
      if (i >= N) {
        list.push_back(saved[i]);
        continue;
      }
      T read{};
      if (parse(saved[i], &read) && read == now[part][i]) list[i] = saved[i];
    }
  }
}

}  // namespace

std::string StateStore::save(const Synth& synth) const {
  nlohmann::json root = nlohmann::json::parse(extraTopLevel_);
  nlohmann::json params = nlohmann::json::parse(extraParams_);
  for (size_t i = 0; i < kParamCount; ++i) {
    const uint32_t id = paramSpec(i).id;
    params[std::to_string(id)] = synth.param(id);
  }
  root["schemaVersion"] = kSchemaVersion;
  root["params"] = std::move(params);

  const EngineStash stash = synth.engineStash();
  for (const Section& section : kSections) {
    nlohmann::json engines = nlohmann::json::object();
    for (int part = 0; part < kNumParts; ++part) {
      nlohmann::json perPart = nlohmann::json::object();
      for (int e = 0; e < slotEngineCount(section.kind); ++e) {
        nlohmann::json values = nlohmann::json::array();
        for (int p = 0; p < int(slotParamCount(section.kind)); ++p) {
          values.push_back(stash[slotStashIndex(section.kind, part, e, p)]);
        }
        if (isModSlot(section.kind)) values.push_back(stash[scopeStashIndex(part, modSlotIndex(section.kind), e)]);
        perPart[std::to_string(e)] = std::move(values);
      }
      engines[std::to_string(part)] = std::move(perPart);
    }
    root[section.key] = std::move(engines);
  }
  const PatchData patch = synth.patch();
  nlohmann::json routes = nlohmann::json::object();
  nlohmann::json shapes = nlohmann::json::object();
  for (int part = 0; part < kNumParts; ++part) {
    nlohmann::json list = nlohmann::json::array();
    for (const Route& r : patch.routes[size_t(part)]) list.push_back({r.source, r.destination, r.via});
    routes[std::to_string(part)] = std::move(list);
    nlohmann::json slots = nlohmann::json::array();
    for (const EnvelopeShape& shape : patch.shapes[size_t(part)]) {
      nlohmann::json types = nlohmann::json::array();
      nlohmann::json loops = nlohmann::json::array();
      for (int i = 0; i < EnvelopeShape::kMaxSegments; ++i) {
        types.push_back(int(shape.type[i]));
        loops.push_back(int(shape.loop[i]));
      }
      slots.push_back({{"count", int(shape.count)}, {"types", std::move(types)}, {"loops", std::move(loops)}});
    }
    shapes[std::to_string(part)] = std::move(slots);
  }
  mergeExtras(routes, loadedRoutes_, patch.routes, &parseRoute);
  mergeExtras(shapes, loadedShapes_, patch.shapes, &parseShape);
  root["routes"] = std::move(routes);
  root["envelopeShapes"] = std::move(shapes);
  return root.dump();
}

LoadResult StateStore::load(Synth& synth, const std::string& json) {
  const nlohmann::json root = nlohmann::json::parse(json, nullptr, false);
  if (root.is_discarded() || !root.is_object()) return LoadResult::Invalid;

  const auto version = root.find("schemaVersion");
  const auto params = root.find("params");
  if (version == root.end() || !version->is_number_integer() || params == root.end() ||
      !params->is_object()) {
    return LoadResult::Invalid;
  }
  const long long schema = version->get<long long>();
  if (schema < 1) return LoadResult::Invalid;
  bool warnings = schema > kSchemaVersion;

  ParamSnapshot values = synth.defaultSnapshot();
  nlohmann::json extraParams = nlohmann::json::object();
  for (auto it = params->begin(); it != params->end(); ++it) {
    uint32_t id = 0;
    if (!parseId(it.key(), &id) || !it.value().is_number()) {
      warnings = true;
      continue;
    }
    const int index = paramIndex(id);
    if (index < 0) {
      extraParams[it.key()] = it.value();
      warnings = true;
      continue;
    }
    values[size_t(index)] = clampParam(size_t(index), it.value().get<double>());
  }

  if (schema < 3) {
    // Schema 2 had no engine selector: its P values belong to the Macro Oscillator.
    for (int part = 0; part < kNumParts; ++part) values[sourceEngineParamIndex(part)] = 0.0f;
  }

  if (schema < 4) {
    // Schema 3 (M2) had no Filter or Shaper: those projects keep their sound with both Off,
    // whose P values are unused.
    for (int part = 0; part < kNumParts; ++part) {
      values[filterEngineParamIndex(part)] = 0.0f;
      values[shaperEngineParamIndex(part)] = 0.0f;
      for (int p = 0; p < int(kFilterParamCount); ++p) values[filterParamIndex(part, p)] = 0.0f;
      for (int p = 0; p < int(kShaperParamCount); ++p) values[shaperParamIndex(part, p)] = 0.0f;
    }
  }

  PatchData patch;  // no routes, ADSR custom shapes
  if (schema < 5) {
    // Schema 4 (M3) had no modulation: those projects load with every mod slot empty (Off, its
    // P's at 0 as in Off's stash entry), no routes, and their old level (Voice Gain 0 dB, Soft
    // Clip off). With no route to the Amp, the Amp follows the gate (spec §5.2).
    for (int part = 0; part < kNumParts; ++part) {
      for (int slot = 0; slot < kNumModSlots; ++slot) {
        values[modEngineParamIndex(part, slot)] = 0.0f;
        values[modScopeParamIndex(part, slot)] = 0.0f;
        for (int p = 0; p < int(kModSlotParamCount); ++p) values[modParamIndex(part, slot, p)] = 0.0f;
      }
      for (int r = 0; r < kNumRoutes; ++r) values[routeAmountParamIndex(part, r)] = 0.0f;
    }
    values[masterParamIndex(MasterParam::VoiceGain)] = 1.0f;
    values[masterParamIndex(MasterParam::SoftClip)] = 0.0f;
  }
  const bool newer = schema > kSchemaVersion;
  if (!readPatchSection(root, "routes", newer, patch.routes, &parseRoute)) warnings = true;
  if (!readPatchSection(root, "envelopeShapes", newer, patch.shapes, &parseShape)) warnings = true;
  // The patch data as loaded, for what this version cannot hold (see mergeExtras).
  std::string routesDump, shapesDump;
  const auto routesIt = root.find("routes");
  const auto shapesIt = root.find("envelopeShapes");
  if (routesIt != root.end() && routesIt->is_object()) routesDump = routesIt->dump();
  if (shapesIt != root.end() && shapesIt->is_object()) shapesDump = shapesIt->dump();

  EngineStash stash = Synth::defaultStash();
  for (const Section& section : kSections) {
    if (!readSection(root, section, stash)) warnings = true;
  }

  nlohmann::json extraTopLevel = nlohmann::json::object();
  for (auto it = root.begin(); it != root.end(); ++it) {
    if (!isKnownTopLevel(it.key())) {
      extraTopLevel[it.key()] = it.value();
      warnings = true;
    }
  }

  std::string topDump = extraTopLevel.dump();
  std::string paramsDump = extraParams.dump();
  synth.loadParams(values, &stash, &patch);
  extraTopLevel_ = std::move(topDump);
  extraParams_ = std::move(paramsDump);
  loadedRoutes_ = std::move(routesDump);
  loadedShapes_ = std::move(shapesDump);
  return warnings ? LoadResult::OkWithWarnings : LoadResult::Ok;
}

}  // namespace eg13::state
