#pragma once

#include <cstddef>
#include <memory>

#include "engine/voice_engine.h"

namespace eg13 {

// Adapter for the upstream Plaits voice, shown as "Macro Oscillator".
// M0: runs Plaits at the host rate with a pitch correction; M1 moves it
// into NativeRateWrapper.
class PlaitsEngine final : public VoiceEngine {
 public:
  static constexpr int kNumModels = 24;

  PlaitsEngine();
  ~PlaitsEngine() override;

  void prepare(double hostRate, int maxBlock) override;
  void reset() override;
  void noteOn(float note, float velocity) override;
  void noteOff() override;
  void process(const float* const* in, float* const* out, const ParamValues& p,
               int n) override;
  const EngineInfo& info() const override;

  static const EngineInfo& staticInfo();
  static int modelIndex(float normalized);
  static float frequencyOffset(float normalized);

 private:
  struct Impl;
  void renderBlock(const ParamValues& p);

  std::unique_ptr<Impl> impl_;
  float pitchOffset_ = 0.0f;
  float note_ = 60.0f;
  float velocity_ = 0.0f;
  bool gate_ = false;
  bool retrigger_ = false;
  size_t pos_ = 0;
};

}  // namespace eg13
