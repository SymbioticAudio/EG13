#pragma once

#include <memory>

#include "engine/pot_smoother.h"
#include "engine/rings_engine.h"
#include "engine/voice_engine.h"

namespace eg13 {

// Part FX "Resonator": the Source Resonator (RingsEngine, Rings) with its Exciter fixed to Input,
// so the part's sound excites the resonator, run at its native 48 kHz in 24-sample blocks
// (rings/dsp/dsp.h:39-41). P1 Mix, P2 Structure, P3 Brightness, P4 Damping, P5 Spread,
// P6 Position, P7-P9 unused, P10 Model, P11 Polyphony, P12 unused, P13 Tune, P14-P16 unused
// (M5 plan ruling 8). Each note-on strums the resonator at that note (ruling 18); setNote
// retunes it without a strum.
class RingsFxEngine final : public VoiceEngine {
 public:
  static constexpr double kNativeRate = RingsEngine::kNativeRate;
  static constexpr int kNativeBlock = RingsEngine::kNativeBlock;

  RingsFxEngine();
  ~RingsFxEngine() override;

  void prepare(double hostRate, int maxBlock) override;
  void reset() override;
  void noteOn(float note, float velocity) override;
  void noteOff() override {}
  void setNote(float note) override;
  void process(const float* const* in, float* const* out, const ParamValues& p, int n) override;
  const EngineInfo& info() const override;

  static const EngineInfo& staticInfo();
  // The Source Resonator's P's for these Part FX P's: Exciter fixed to Input (1.5 / 3), Model from
  // P10 in 16 steps over 6 models, Polyphony from P11 over 1 / 2 / 4, Frequency = P13,
  // Spread = P5, Position = P6.
  static ParamValues sourceParams(const ParamValues& fx);

 private:
  std::unique_ptr<RingsEngine> inner_;
  ParamRamp mix_;  // P1, linear across each native block
};

}  // namespace eg13
