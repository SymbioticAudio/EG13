#include "state/state.h"

#include <cstdint>
#include <cstdlib>
#include <utility>

#include <nlohmann/json.hpp>

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

  nlohmann::json extraTopLevel = nlohmann::json::object();
  for (auto it = root.begin(); it != root.end(); ++it) {
    if (it.key() != "schemaVersion" && it.key() != "params") {
      extraTopLevel[it.key()] = it.value();
      warnings = true;
    }
  }

  std::string topDump = extraTopLevel.dump();
  std::string paramsDump = extraParams.dump();
  synth.loadParams(values);
  extraTopLevel_ = std::move(topDump);
  extraParams_ = std::move(paramsDump);
  return warnings ? LoadResult::OkWithWarnings : LoadResult::Ok;
}

}  // namespace eg13::state
