#pragma once

#include <vector>

#include "engine/pot_smoother.h"
#include "engine/ripples_model.h"
#include "engine/stereo_link.h"
#include "engine/voice_engine.h"

namespace eg13 {

// Adapter for the VCV Ripples model (AudibleInstruments src/Ripples, GPL-3.0-or-later), shown as
// "Analog Filter". Runs at the host rate (the model oversamples internally, aafilter.hpp). The
// model itself is RipplesModel, built with Rack's flags; this adapter is built without them.
class RipplesEngine final : public VoiceEngine {
 public:
  static constexpr float kVolts = 5.0f;  // EG13 ±1.0 = ±5 V, VCV's audio level

  RipplesEngine();
  ~RipplesEngine() override;

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
  static int modeIndex(float normalized);  // P3 thirds: 0 LP4, 1 LP2, 2 BP2
  float smoothedParam(int index) const;    // test hook: P1/P2 where the last block ended
  bool rightRuns() const { return link_.rightRuns(); }

 private:
  RipplesModel left_;
  RipplesModel right_;
  std::vector<float> rightOut_;
  double rate_ = 48000.0;
  bool stereo_ = false;
  ParamRamp freq_;
  ParamRamp res_;
  StereoLink link_;
};

}  // namespace eg13
