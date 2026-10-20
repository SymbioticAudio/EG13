#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "engine/voice_engine.h"

namespace r8b {
class CDSPResampler24;
}

namespace eg13 {

// Runs an engine at its native sample rate and block size, and converts its
// output to the host rate with r8brain (linear phase). At the native rate it is
// a bit-exact pass-through. Notes and parameters reach the engine at native
// block boundaries, which is the hardware's control rate.
class NativeRateWrapper final : public VoiceEngine {
 public:
  // ~2.3 ms of latency at every common host rate (r8brain 7.6, measured).
  static constexpr double kTransitionBandPercent = 30.0;

  NativeRateWrapper(std::unique_ptr<VoiceEngine> inner, double nativeRate, int nativeBlock);
  ~NativeRateWrapper() override;

  void prepare(double hostRate, int maxBlock) override;
  void reset() override;
  void noteOn(float note, float velocity) override;
  void noteOff() override;
  void setNote(float note) override;
  // M1: `in` is ignored. Converting input to the native rate arrives with its
  // first consumer (M2).
  void process(const float* const* in, float* const* out, const ParamValues& p,
               int n) override;
  const EngineInfo& info() const override;

  int latencySamples() const { return latency_; }  // host-rate samples

 private:
  void renderNativeBlock(const ParamValues& p);

  std::unique_ptr<VoiceEngine> inner_;
  double nativeRate_;
  int nativeBlock_;
  bool bypass_ = true;
  int latency_ = 0;
  std::unique_ptr<r8b::CDSPResampler24> resampler_[2];
  std::vector<float> native_[2];
  std::vector<double> scratch_;
  std::vector<float> fifo_[2];
  size_t fifoRead_ = 0;
  size_t fifoSize_ = 0;
  size_t zeroPad_ = 0;  // host samples of silence owed after reset()
};

}  // namespace eg13
