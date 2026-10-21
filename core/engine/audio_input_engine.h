#pragma once

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
  float smoothedGain() const { return gain_; }

 private:
  double rate_ = 48000.0;
  float velocity_ = 0.0f;
  bool gate_ = false;
  double envelope_ = 0.0;
  float gain_ = 1.0f;             // linear Gain after its de-click one-pole
  float gainCoeff_ = 0.0206193f;  // that one-pole per sample; 1 ms at 48 kHz until prepare
  bool snap_ = true;              // the first render after reset jumps straight to the target
};

}  // namespace eg13
