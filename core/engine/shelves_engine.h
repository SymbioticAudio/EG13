#pragma once

#include <vector>

#include "engine/pot_smoother.h"
#include "engine/shelves_model.h"
#include "engine/stereo_link.h"
#include "engine/voice_engine.h"

namespace eg13 {

// Adapter for the VCV Shelves model (AudibleInstruments src/Shelves, GPL-3.0-or-later), shown as
// "EQ". Runs at the host rate (the model oversamples internally, aafilter.hpp). The model itself
// is ShelvesModel, built with Rack's flags; this adapter is built without them.
class ShelvesEngine final : public VoiceEngine {
 public:
  static constexpr float kVolts = 5.0f;  // EG13 ±1.0 = ±5 V, VCV's audio level
  static constexpr int kNumOutputs = 7;  // EQ, Mid 1 LP/BP/HP, Mid 2 LP/BP/HP

  ShelvesEngine();
  ~ShelvesEngine() override;

  void prepare(double hostRate, int maxBlock) override;
  void reset() override;  // audio-thread safe: no allocation
  void noteOn(float, float) override {}
  void noteOff() override {}
  void setNote(float) override {}
  void setStereo(bool stereo) override { stereo_ = stereo; }
  void process(const float* const* in, float* const* out, const ParamValues& p,
               int n) override;
  const EngineInfo& info() const override;

  static const EngineInfo& staticInfo();
  static float gainDb(float normalized);   // -18..+18 dB (shelves.hpp:30)
  static float qFactor(float normalized);  // 0.5..40, exponential (shelves.hpp:31-32)
  static int outputIndex(float normalized);
  static bool padOn(float normalized);
  float smoothedParam(int index) const;  // test hook: P1..P10 where the last block ended
  bool rightRuns() const { return link_.rightRuns(); }

 private:
  static constexpr int kNumKnobs = 10;

  ShelvesModel left_;
  ShelvesModel right_;
  std::vector<float> rightOut_;
  double rate_ = 48000.0;
  bool stereo_ = false;
  ParamRamp ramps_[kNumKnobs];
  StereoLink link_;
};

}  // namespace eg13
