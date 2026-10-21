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
  // Cap on cutoff / host rate. The accurate tangent stmlib's Svf uses here is a polynomial that
  // never blows up, so the cap only bites at host rates below about 40 kHz, where 20 kHz would
  // pass half the rate; it is the exact tangent's clip (filter.h:110), kept for those rates.
  static constexpr float kMaxF = 0.497f;

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
