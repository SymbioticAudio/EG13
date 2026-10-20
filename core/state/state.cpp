#include "state/state.h"

#include <cstdint>
#include <cstdlib>
#include <utility>
#include <vector>

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

}  // namespace

std::string save(const Synth& synth) {
  nlohmann::json params = nlohmann::json::object();
  for (size_t i = 0; i < kParamCount; ++i) {
    const uint32_t id = paramSpec(i).id;
    params[std::to_string(id)] = synth.param(id);
  }
  nlohmann::json root = nlohmann::json::object();
  root["schemaVersion"] = kSchemaVersion;
  root["params"] = std::move(params);
  return root.dump();
}

LoadResult load(Synth& synth, const std::string& json) {
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

  std::vector<std::pair<uint32_t, double>> values;
  for (auto it = params->begin(); it != params->end(); ++it) {
    uint32_t id = 0;
    if (!parseId(it.key(), &id) || !it.value().is_number() || paramIndex(id) < 0) {
      warnings = true;
      continue;
    }
    values.emplace_back(id, it.value().get<double>());
  }

  synth.resetParamsToDefaults();
  for (const auto& [id, value] : values) {
    if (!synth.setParam(id, value)) warnings = true;
  }
  return warnings ? LoadResult::OkWithWarnings : LoadResult::Ok;
}

}  // namespace eg13::state
