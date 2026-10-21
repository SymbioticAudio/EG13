#include "state/state.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
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

// "routes": {part: [[source, destination, via], ... up to 12]}. Codes are kept as saved (unknown
// ones too: a newer version's routes come back unchanged on resave); entries that are not three
// integers 0..65535 are dropped. False if anything had to be dropped.
bool readRoutes(const nlohmann::json& root, PatchData& patch) {
  const auto found = root.find("routes");
  if (found == root.end()) return true;
  if (!found->is_object()) return false;
  bool clean = true;
  for (auto partIt = found->begin(); partIt != found->end(); ++partIt) {
    uint32_t part = 0;
    if (!parseId(partIt.key(), &part) || part >= uint32_t(kNumParts) || !partIt.value().is_array() ||
        partIt.value().size() > size_t(kNumRoutes)) {
      clean = false;
      continue;
    }
    const auto& list = partIt.value();
    for (size_t r = 0; r < list.size(); ++r) {
      const auto& entry = list[r];
      bool valid = entry.is_array() && entry.size() == 3;
      for (size_t k = 0; valid && k < 3; ++k) {
        valid = entry[k].is_number_integer() && entry[k].get<long long>() >= 0 && entry[k].get<long long>() <= 65535;
      }
      if (!valid) {
        clean = false;
        continue;
      }
      patch.routes[part][r] = {uint16_t(entry[0].get<long long>()), uint16_t(entry[1].get<long long>()),
                               uint16_t(entry[2].get<long long>())};
    }
  }
  return clean;
}

// "envelopeShapes": {part: [{"count": n, "types": [6], "loops": [6]}, ... 4 slots]}. A malformed
// shape keeps the default (ADSR); values out of range are sanitized.
bool readShapes(const nlohmann::json& root, PatchData& patch) {
  const auto found = root.find("envelopeShapes");
  if (found == root.end()) return true;
  if (!found->is_object()) return false;
  bool clean = true;
  for (auto partIt = found->begin(); partIt != found->end(); ++partIt) {
    uint32_t part = 0;
    if (!parseId(partIt.key(), &part) || part >= uint32_t(kNumParts) || !partIt.value().is_array() ||
        partIt.value().size() > size_t(kNumModSlots)) {
      clean = false;
      continue;
    }
    const auto& list = partIt.value();
    for (size_t slot = 0; slot < list.size(); ++slot) {
      const auto& e = list[slot];
      const auto count = e.is_object() ? e.find("count") : e.end();
      const auto types = e.is_object() ? e.find("types") : e.end();
      const auto loops = e.is_object() ? e.find("loops") : e.end();
      bool valid = e.is_object() && count != e.end() && count->is_number_integer() && types != e.end() &&
                   types->is_array() && types->size() == size_t(EnvelopeShape::kMaxSegments) && loops != e.end() &&
                   loops->is_array() && loops->size() == size_t(EnvelopeShape::kMaxSegments);
      for (size_t i = 0; valid && i < size_t(EnvelopeShape::kMaxSegments); ++i) {
        valid = (*types)[i].is_number_integer() && (*loops)[i].is_number_integer();
      }
      if (!valid) {
        clean = false;
        continue;
      }
      EnvelopeShape shape;
      shape.count = uint8_t(std::clamp(count->get<long long>(), 0LL, 255LL));
      for (size_t i = 0; i < size_t(EnvelopeShape::kMaxSegments); ++i) {
        shape.type[i] = uint8_t(std::clamp((*types)[i].get<long long>(), 0LL, 255LL));
        shape.loop[i] = uint8_t(std::clamp((*loops)[i].get<long long>(), 0LL, 255LL));
      }
      patch.shapes[part][slot] = sanitized(shape);
    }
  }
  return clean;
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
  if (!readRoutes(root, patch)) warnings = true;
  if (!readShapes(root, patch)) warnings = true;

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
  return warnings ? LoadResult::OkWithWarnings : LoadResult::Ok;
}

}  // namespace eg13::state
