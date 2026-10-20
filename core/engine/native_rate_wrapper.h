#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "engine/voice_engine.h"

namespace r8b {
class CDSPResampler24;
}

namespace eg13 {

// Runs an engine at its native sample rate and block size, converting its
// output (and, for engines that read input, the host input) with r8brain.
// At the native rate it is a bit-exact pass-through. Notes and parameters
// reach the engine at native block boundaries (the hardware's control rate).
// nativeRate <= 0 runs the engine at the host rate.
class NativeRateWrapper final : public VoiceEngine {
 public:
  // ~2.3 ms of latency at every common host rate (r8brain 7.6, measured).
  static constexpr double kTransitionBandPercent = 30.0;

  NativeRateWrapper(std::unique_ptr<VoiceEngine> inner, double nativeRate, int nativeBlock);
  ~NativeRateWrapper() override;

  // Main thread. Resets the extra delay: call setExtraDelay after every prepare.
  void prepare(double hostRate, int maxBlock) override;
  void reset() override;
  void noteOn(float note, float velocity) override;
  void noteOff() override;
  void setNote(float note) override;
  // in: host-rate input (nullable = silence); converted for engines that use it.
  void process(const float* const* in, float* const* out, const ParamValues& p,
               int n) override;
  const EngineInfo& info() const override;

  void setExtraDelay(int samples);  // main thread, after prepare (allocates)

  int outputLatencySamples() const { return latency_; }
  int inputLatencySamples() const { return inputLatency_; }
  int extraDelaySamples() const { return extra_; }
  int latencySamples() const { return latency_ + extra_; }
  int inputPathLatencySamples() const { return inputLatency_ + extra_; }
  size_t inputUnderruns() const { return underruns_; }
  size_t inputOverruns() const { return overruns_; }

 private:
  void pushInput(const float* const* in, int n);
  void renderNativeBlock(const ParamValues& p);
  void applyExtraDelay(float* const* out, int n);

  std::unique_ptr<VoiceEngine> inner_;
  double nativeRate_;
  int nativeBlock_;
  bool bypass_ = true;
  bool usesInput_ = false;
  int latency_ = 0;
  int inputLatency_ = 0;

  // Output path: native -> host.
  std::unique_ptr<r8b::CDSPResampler24> resampler_[2];
  std::vector<float> native_[2];
  std::vector<double> scratch_;
  std::vector<float> fifo_[2];
  size_t fifoRead_ = 0;
  size_t fifoSize_ = 0;
  size_t zeroPad_ = 0;  // host samples of silence owed after reset()

  // Input path: host -> native.
  std::unique_ptr<r8b::CDSPResampler24> inputResampler_[2];
  std::vector<double> inputScratch_;
  std::vector<float> inputFifo_[2];  // ring buffer
  size_t inputRead_ = 0;
  size_t inputSize_ = 0;
  size_t inputPrime_ = 0;  // native samples of silence queued at reset()
  std::vector<float> nativeIn_[2];
  size_t underruns_ = 0;
  size_t overruns_ = 0;

  // Alignment delay.
  int extra_ = 0;
  std::vector<float> delay_[2];
  size_t delayPos_ = 0;
};

}  // namespace eg13
