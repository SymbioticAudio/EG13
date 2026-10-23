#include "engine/bypass_engine.h"

namespace eg13 {

namespace {
const EngineInfo kInfo = {"off", "Off", 0, nullptr, true, false, 0, nullptr};
}  // namespace

const EngineInfo& BypassEngine::staticInfo() { return kInfo; }

const EngineInfo& BypassEngine::info() const { return kInfo; }

void BypassEngine::process(const float* const* in, float* const* out, const ParamValues&, int n) {
  for (int i = 0; i < n; ++i) {
    out[0][i] = in != nullptr ? in[0][i] : 0.0f;
    out[1][i] = in != nullptr ? in[1][i] : 0.0f;
  }
}

}  // namespace eg13
