#pragma once

#include "engine/pot_smoother.h"
#include "engine/voice_engine.h"

namespace eg13 {

// The part's input through a per-voice AR gate, shown as "Audio Input".
// Runs at the host rate.
class AudioInputEngine final : public VoiceEngine {
 public:
  void prepare(double hostRate, int maxBlock) override;
  void reset() override;  // audio-thread safe: no allocation
  void noteOn(float note, float velocity) override;
  void noteOff() override { gate_ = false; }
  void setNote(float) override {}
  void process(const float* const* in, float* const* out, const ParamValues& p,
               int n) override;
  const EngineInfo& info() const override;

  static const EngineInfo& staticInfo();
  static float gainDb(float normalized);          // -24..+12 dB
  static float attackSeconds(float normalized);   // 1 ms..2 s
  static float releaseSeconds(float normalized);  // 5 ms..5 s
  // Test hook: the linear gain applied to the last output sample.
  float smoothedGain() const { return gain_.value(); }

 private:
  double rate_ = 48000.0;
  float velocity_ = 0.0f;
  bool gate_ = false;
  double envelope_ = 0.0;
  Declicker gain_{0.001};  // linear Gain, 1 ms de-click one-pole per sample
};

}  // namespace eg13
