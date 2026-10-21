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
// transformation is needed. Later schema changes add their migrations here.

// engineSettings arrays hold kSourceParamCount numbers. Early unreleased
// schema 3 builds wrote 8 (P1..P8); those load as a prefix, the rest keep the
// engine's defaults.
constexpr size_t kLegacyEngineSettingsSize = 8;

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
  nlohmann::json engines = nlohmann::json::object();
  for (int part = 0; part < kNumParts; ++part) {
    nlohmann::json perPart = nlohmann::json::object();
    for (int e = 0; e < kNumSourceEngines; ++e) {
      nlohmann::json values = nlohmann::json::array();
      for (int p = 0; p < int(kSourceParamCount); ++p) values.push_back(stash[stashIndex(part, e, p)]);
      perPart[std::to_string(e)] = std::move(values);
    }
    engines[std::to_string(part)] = std::move(perPart);
  }
  root["engineSettings"] = std::move(engines);
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

  EngineStash stash = Synth::defaultStash();
  const auto engines = root.find("engineSettings");
  if (engines != root.end()) {
    if (!engines->is_object()) {
      warnings = true;
    } else {
      for (auto partIt = engines->begin(); partIt != engines->end(); ++partIt) {
        uint32_t part = 0;
        if (!parseId(partIt.key(), &part) || part >= uint32_t(kNumParts) || !partIt.value().is_object()) {
          warnings = true;
          continue;
        }
        for (auto engineIt = partIt.value().begin(); engineIt != partIt.value().end(); ++engineIt) {
          uint32_t engine = 0;
          const auto& list = engineIt.value();
          bool valid = parseId(engineIt.key(), &engine) && engine < uint32_t(kNumSourceEngines) &&
                       list.is_array() &&
                       (list.size() == kSourceParamCount || list.size() == kLegacyEngineSettingsSize);
          for (size_t p = 0; valid && p < list.size(); ++p) valid = list[p].is_number();
          if (!valid) {
            warnings = true;
            continue;
          }
          for (size_t p = 0; p < list.size(); ++p) {
            const double v = list[p].get<double>();
            stash[stashIndex(int(part), int(engine), int(p))] =
                float(std::clamp(std::isnan(v) ? 0.0 : v, 0.0, 1.0));
          }
        }
      }
    }
  }

  nlohmann::json extraTopLevel = nlohmann::json::object();
  for (auto it = root.begin(); it != root.end(); ++it) {
    if (it.key() != "schemaVersion" && it.key() != "params" && it.key() != "engineSettings") {
      extraTopLevel[it.key()] = it.value();
      warnings = true;
    }
  }

  std::string topDump = extraTopLevel.dump();
  std::string paramsDump = extraParams.dump();
  synth.loadParams(values, &stash);
  extraTopLevel_ = std::move(topDump);
  extraParams_ = std::move(paramsDump);
  return warnings ? LoadResult::OkWithWarnings : LoadResult::Ok;
}

}  // namespace eg13::state
