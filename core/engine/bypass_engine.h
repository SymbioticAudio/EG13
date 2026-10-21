#pragma once

#include "engine/voice_engine.h"

namespace eg13 {

// "Off" in the Filter and Shaper slots: the input passes through unchanged (padded to the
// slot's latency by its wrapper).
class BypassEngine final : public VoiceEngine {
 public:
  void prepare(double, int) override {}
  void reset() override {}
  void noteOn(float, float) override {}
  void noteOff() override {}
  void setNote(float) override {}
  void process(const float* const* in, float* const* out, const ParamValues& p, int n) override;
  const EngineInfo& info() const override;
  static const EngineInfo& staticInfo();
};

}  // namespace eg13
