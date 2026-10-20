#include "synth/param_table.h"

#include <array>
#include <cmath>
#include <cstdio>

#include "engine/plaits_engine.h"
#include "params/param_ids.h"

namespace eg13 {

namespace {

const std::array<ParamSpec, kParamCount>& table() {
  static const std::array<ParamSpec, kParamCount> specs = [] {
    std::array<ParamSpec, kParamCount> t{};
    const EngineInfo& info = PlaitsEngine::staticInfo();
    for (size_t i = 0; i < kSourceParamCount; ++i) {
      t[i] = {params::id(0, params::Slot::Source, uint32_t(i + 1)), info.params[i].name,
              "Part A/Source", info.params[i].defaultValue};
    }
    t[kMasterLevelIndex] = {params::kMasterLevel, "Level", "Master", 0.8f};
    return t;
  }();
  return specs;
}

}  // namespace

const ParamSpec& paramSpec(size_t index) { return table()[index]; }

int paramIndex(uint32_t id) {
  const auto& t = table();
  for (size_t i = 0; i < kParamCount; ++i) {
    if (t[i].id == id) return int(i);
  }
  return -1;
}

void formatParam(size_t index, double value, char* buf, size_t size) {
  const float v = clamp01(float(value));
  if (index < kSourceParamCount) {
    PlaitsEngine::staticInfo().params[index].format(v, buf, size);
  } else {
    std::snprintf(buf, size, "%d%%", int(std::lround(v * 100.0f)));
  }
}

}  // namespace eg13
