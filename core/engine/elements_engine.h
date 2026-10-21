#pragma once

#include <memory>

#include "engine/pot_smoother.h"
#include "engine/voice_engine.h"

namespace eg13 {

// Adapter for the upstream Elements modal voice, shown as "Modal Synth".
class ElementsEngine final : public VoiceEngine {
 public:
  static constexpr double kNativeRate = 32000.0;
  static constexpr int kNativeBlock = 16;
  static constexpr int kNumModels = 3;
  static constexpr int kNumExtIn = 4;

  ElementsEngine();
  ~ElementsEngine() override;

  void prepare(double hostRate, int maxBlock) override;
  void reset() override;
  void noteOn(float note, float velocity) override;
  void noteOff() override;
  void setNote(float note) override { note_ = note; }
  void process(const float* const* in, float* const* out, const ParamValues& p,
               int n) override;
  const EngineInfo& info() const override;

  static const EngineInfo& staticInfo();
  static int modelIndex(float normalized);  // P1 -> 0..2
  static int extInIndex(float normalized);  // P16 -> 0 Off, 1 Blow, 2 Strike, 3 Both
  // Test hook: the smoothed value last written to the Patch for P2..P15 (index 1..14).
  float smoothedParam(int index) const;

 private:
  struct Impl;
  void renderBlock(const float* const* in, const ParamValues& p, float* const* out, int offset,
                   int count);

  std::unique_ptr<Impl> impl_;
  float pitchOffset_ = 0.0f;
  float note_ = 60.0f;
  float velocity_ = 0.0f;
  bool gate_ = false;
  bool deliveredGate_ = false;  // gate level last given to Part
  bool pendingStrike_ = false;  // a note-on not yet delivered as a rising edge
  int model_ = -1;
  // CvScaler-style smoothing of P2..P15: the general pot low-pass and BIND's extra one-pole.
  void smooth(const ParamValues& p, int count);
  PotSmoother pots_;  // the general pot low-pass
  float smoothed_[kMaxSlotParams] = {};
  bool snap_ = true;  // the first render after reset jumps straight to the targets
};

}  // namespace eg13
