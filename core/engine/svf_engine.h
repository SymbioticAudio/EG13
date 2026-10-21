#pragma once

#include <memory>

#include "engine/pot_smoother.h"
#include "engine/stereo_link.h"
#include "engine/voice_engine.h"

namespace eg13 {

// stmlib's state-variable filter (stmlib/dsp/filter.h:177-260), shown as "State Variable
// Filter". No module exposes it on its own, so it runs at the host rate and smooths like the
// Plaits engine that uses it (plaits/dsp/engine/noise_engine.cc:76-91).
class SvfEngine final : public VoiceEngine {
 public:
  static constexpr float kMaxF = 0.497f;  // f / rate clip, as stmlib's exact tangent (filter.h:110)

  SvfEngine();
  ~SvfEngine() override;

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
  static float resonanceQ(float normalized);  // 0.5 .. 40, exponential
  static int modeIndex(float normalized);     // P3 thirds: 0 LP, 1 BP, 2 HP
  float smoothedParam(int index) const;       // test hook: f (index 0) or Q (1) at block end
  bool rightRuns() const { return link_.rightRuns(); }

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  double rate_ = 48000.0;
  bool stereo_ = false;
  ParamRamp f_;
  ParamRamp q_;
  StereoLink link_;
};

}  // namespace eg13
