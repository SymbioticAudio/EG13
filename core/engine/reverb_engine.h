#pragma once

#include <memory>

#include "engine/pot_smoother.h"
#include "engine/voice_engine.h"

namespace eg13 {

// Part FX "Reverb": the reverb of Elements (elements/dsp/fx/reverb.h) on its own, at its native
// 32 kHz in 16-sample blocks (elements/dsp/dsp.h:38-39). P1 Amount, P2 Time, P3 Diffusion,
// P4 Brightness, P5-P8 unused, P9 Freeze. Notes do nothing.
class ReverbEngine final : public VoiceEngine {
 public:
  static constexpr double kNativeRate = 32000.0;
  static constexpr int kNativeBlock = 16;

  ReverbEngine();
  ~ReverbEngine() override;

  void prepare(double hostRate, int maxBlock) override;
  void reset() override;
  void noteOn(float, float) override {}
  void noteOff() override {}
  void setNote(float) override {}
  void process(const float* const* in, float* const* out, const ParamValues& p, int n) override;
  const EngineInfo& info() const override;

  static const EngineInfo& staticInfo();
  // The laws (M5 plan ruling 19), on the smoothed 0..1 P.
  static float timeFor(float p);       // Time: 0.35 + 0.63 P (Clouds' range for this reverb)
  static float diffusionFor(float p);  // Diffusion: 0.55 + 0.15 P (Elements' range)
  static float lpFor(float p);         // Brightness: 0.6 + 0.37 P (Clouds' range)
  // Test hook: the smoothed 0..1 value of P1..P9 (index 0..8) last given to the reverb.
  float smoothedParam(int index) const;

 private:
  struct Impl;
  void renderBlock(const float* const* in, float* const* out, const ParamValues& p, int offset, int count);

  std::unique_ptr<Impl> impl_;
  PotSmoother pots_;  // Elements' pot filter, once per native block
};

}  // namespace eg13
