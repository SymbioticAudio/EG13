#pragma once

#include <cstddef>
#include <cstdint>
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
// Channels: in[0], in[1] (+ in[2], in[3], the engine's side inputs); out[0], out[1]
// (+ out[2], AUX, for hasAux engines with AUX enabled). Every channel is converted alike, so
// they stay sample-aligned.
// A channel that equals its reference (in[1] and out[1]: channel 0; a side input: silence, or
// null) for one converter span is not converted: it takes the reference's converted signal,
// which is what its converter would produce. When it differs again its converter restarts, is
// pre-rolled with the reference's recent history and joins in step (see resume()).
class NativeRateWrapper final : public VoiceEngine {
 public:
  // ~2.3 ms for Plaits-rate engines; up to ~4.6 ms for 32 kHz engines (r8brain 7.6, measured).
  static constexpr double kTransitionBandPercent = 30.0;
  static constexpr int kMaxChannels = 4;  // inputs L, R and two side inputs; outputs L, R, AUX

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
  void setStereo(bool stereo) override;
  void setAuxEnabled(bool on) override;  // main thread, before prepare

  void setExtraDelay(int samples);  // main thread, after prepare (allocates)

  // All latencies include the engine's own nativeLatency(), in host samples.
  int outputLatencySamples() const { return latency_; }
  int inputLatencySamples() const { return inputLatency_; }
  int extraDelaySamples() const { return extra_; }
  int latencySamples() const { return latency_ + extra_; }
  int inputPathLatencySamples() const { return inputLatency_ + extra_; }
  size_t inputUnderruns() const { return underruns_; }
  size_t inputOverruns() const { return overruns_; }
  // Channels converted at the last conversion of the input and the output path (tests).
  int convertedInputChannels() const;
  int convertedOutputChannels() const;
  // Tests: false converts every channel always (the reference behaviour). Main thread.
  void setSkipUnused(bool on) { skipUnused_ = on; }

 private:
  // A channel whose conversion can be skipped while it equals its reference.
  struct Skip {
    bool skipped = false;
    size_t run = 0;  // samples in a row equal to the reference
  };
  // The converter path of one direction: the period and span of its converters, in their input
  // samples, and a history of channel 0's input to pre-roll a restarted converter with.
  struct Path {
    int64_t period = 0;  // 0: never skip
    int64_t span = 0;
    int64_t fed = 0;     // samples fed to channel 0's converter since clear()
    std::vector<float> history;  // ring: channel 0's input; sample k at k % history.size()
    void record(const float* x, int n);
    void reset();
  };
  static void setUpPath(Path& path, double from, double to, int latencyIn, int maxChunk);
  // Restarts converter r and pre-rolls it with the reference's last samples (channel 0's
  // history, or silence), so that from now on it runs in step with channel 0's converter.
  static void resume(r8b::CDSPResampler24& r, const Path& path, bool silence, int maxChunk,
                     std::vector<double>& scratch);
  void pushInput(const float* const* in, int n);
  void renderNativeBlock(const ParamValues& p);
  void applyExtraDelay(float* const* out, int n);

  std::unique_ptr<VoiceEngine> inner_;
  double nativeRate_;
  int nativeBlock_;
  bool bypass_ = true;
  bool usesInput_ = false;
  bool auxEnabled_ = false;
  int inChannels_ = 0;   // 0, or 2 + the engine's side inputs
  int outChannels_ = 2;  // 2, or 3 with AUX
  int latency_ = 0;
  int inputLatency_ = 0;
  int engineLatency_ = 0;  // the engine's nativeLatency() in host samples (part of both above)

  // Output path: native -> host.
  std::unique_ptr<r8b::CDSPResampler24> resampler_[kMaxChannels];
  std::vector<float> native_[kMaxChannels];
  std::vector<double> scratch_;
  std::vector<float> fifo_[kMaxChannels];
  size_t fifoRead_ = 0;
  size_t fifoSize_ = 0;
  size_t zeroPad_ = 0;  // host samples of silence owed after reset()

  // Input path: host -> native.
  std::unique_ptr<r8b::CDSPResampler24> inputResampler_[kMaxChannels];
  std::vector<double> inputScratch_;
  std::vector<float> inputFifo_[kMaxChannels];  // ring buffers
  size_t inputRead_ = 0;
  size_t inputSize_ = 0;
  size_t inputPrime_ = 0;  // native samples of silence queued at reset()
  std::vector<float> nativeIn_[kMaxChannels];
  size_t underruns_ = 0;
  size_t overruns_ = 0;

  // Skipping unused conversions.
  bool skipUnused_ = true;
  Skip inSkip_[kMaxChannels];
  Skip outSkip_[kMaxChannels];
  Path inPath_;
  Path outPath_;
  std::vector<double> silence_;     // converted silence for skipped side inputs
  std::vector<double> preroll_;     // pre-roll chunk
  int lastInConverted_ = 0;
  int lastOutConverted_ = 0;

  // Alignment delay.
  int extra_ = 0;
  std::vector<float> delay_[kMaxChannels];
  size_t delayPos_ = 0;
};

}  // namespace eg13
