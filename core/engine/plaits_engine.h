#pragma once

#include <cstddef>
#include <memory>

#include "engine/pot_smoother.h"
#include "engine/voice_engine.h"

namespace eg13 {

// Adapter for the upstream Plaits voice, shown as "Macro Oscillator".
// M0: runs Plaits at the host rate with a pitch correction; M1 moves it
// into NativeRateWrapper.
class PlaitsEngine final : public VoiceEngine {
 public:
  static constexpr int kNumModels = 24;
  static constexpr int kNativeBlock = 12;  // plaits::kBlockSize
  static constexpr double kNativeRate = 47872.34;  // plaits::kCorrectedSampleRate: the module's real clock, which Plaits' DSP is tuned for

  PlaitsEngine();
  ~PlaitsEngine() override;

  void prepare(double hostRate, int maxBlock) override;
  void reset() override;
  void noteOn(float note, float velocity) override;
  void noteOff() override;
  void setNote(float note) override { note_ = note; }
  void setAuxEnabled(bool on) override { aux_ = on; }
  void process(const float* const* in, float* const* out, const ParamValues& p,
               int n) override;
  const EngineInfo& info() const override;

  static const EngineInfo& staticInfo();
  static int modelIndex(float normalized);
  static float frequencyOffset(float normalized);
  // Test hook: the smoothed 0..1 value of P1..P8 (index 0..7) last given to the voice.
  float smoothedParam(int index) const;
  // Test hook: the de-clicked Aux Mix applied to the last output sample.
  float smoothedAuxMix() const { return auxMix_.value(); }

 private:
  struct Impl;
  void renderBlock(const ParamValues& p);

  std::unique_ptr<Impl> impl_;
  float pitchOffset_ = 0.0f;
  float note_ = 60.0f;
  float velocity_ = 0.0f;
  bool gate_ = false;
  bool retrigger_ = false;
  bool aux_ = false;  // write out[2]: Plaits' AUX output, for the Ring Mod modulator
  size_t pos_ = 0;
  PotSmoother pots_;  // Ui::Poll pot smoothing, once per native block
  Declicker auxMix_;  // EG13-only Aux Mix, per sample
};

}  // namespace eg13
