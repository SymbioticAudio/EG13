#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "engine/voice_engine.h"

namespace r8b {
class CDSPResampler;
}

namespace eg13 {

// The filter phase of a converter: EG13's own enum, mapped to r8brain's in the .cpp (the plugin
// target does not see r8brain's headers).
enum class ConverterPhase : int { Linear = 0, Minimum = 1 };

// How a NativeRateWrapper builds its converters (r8b::CDSPResampler).
struct ConverterSpec {
  ConverterPhase phase;
  double attenuationDb;          // stop-band attenuation (r8brain ReqAtten)
  double transitionBandPercent;  // r8brain ReqTransBand; kTransitionBandPercent in every mode
  int primeBlocks;  // native blocks of silence the input path holds in hand (+ withheld + 1)
};

// The converters of each Latency Mode. Studio is the converter EG13 shipped before the modes
// (r8b::CDSPResampler24); Live is chosen by measurement (see the .cpp).
ConverterSpec converterSpec(ConverterQuality q);

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
// pre-rolled with the reference's recent history and joins in step (see resume()). prepare()
// proves this on scratch converters for each path and disables skipping where it fails; a
// resumption that still comes back out of step turns skipping off for that path until the
// next prepare(), after restarting every conversion (see fallBackInput()).
class NativeRateWrapper final : public VoiceEngine {
 public:
  // ~2.3 ms for Plaits-rate engines; up to ~4.6 ms for 32 kHz engines (r8brain 7.6, measured).
  static constexpr double kTransitionBandPercent = 30.0;
  static constexpr int kMaxChannels = 4;  // inputs L, R and two side inputs; outputs L, R, AUX

  NativeRateWrapper(std::unique_ptr<VoiceEngine> inner, double nativeRate, int nativeBlock);
  ~NativeRateWrapper() override;

  // Main thread. Takes effect at the next prepare(): every converter of the wrapper (and of the
  // engine, through setConverterQuality) is built from converterSpec(q). Default: Studio.
  void setQuality(ConverterQuality q);
  ConverterQuality quality() const { return quality_; }
  void setConverterQuality(ConverterQuality q) override { setQuality(q); }
  // Tests (the Live candidate sweep): build the converters from `spec` at the next prepare().
  void setSpecForTest(const ConverterSpec& spec) { spec_ = spec; }
  const ConverterSpec& spec() const { return spec_; }

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
  // Whether each path may skip: off when the rates have no exact converter period, when the
  // prepare-time check of a resumption failed, or after a fallback.
  bool inputSkipEnabled() const { return inPath_.period > 0; }
  bool outputSkipEnabled() const { return outPath_.period > 0; }
  // Times a resumed converter came back out of step and its path fell back to always converting.
  size_t skipFallbacks() const { return fallbacks_; }
  // Tests only, after prepare: replaces the paths' periods without the prepare-time check
  // (0 disables), to exercise the fallback.
  void forceSkipPeriodsForTest(int64_t input, int64_t output);

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
    size_t write = 0;            // fed % history.size()
    void record(const float* x, int n);
    void reset();
  };
  // Sets up a path from `from` to `to` for converters fed at most maxChunk samples per call,
  // with chunks of `chunk` samples (0: varying) at run time.
  static void setUpPath(Path& path, const ConverterSpec& spec, double from, double to, int latencyIn,
                        int maxChunk, int chunk);
  static int64_t converterPeriod(double from, double to);
  static bool checkResume(const ConverterSpec& spec, double from, double to, int maxChunk, int chunk,
                          const Path& setUp);
  // Restarts converter r and pre-rolls it with the reference's last samples (channel 0's
  // history, or silence), so that from now on it runs in step with channel 0's converter.
  static void resume(r8b::CDSPResampler& r, const Path& path, bool silence, int maxChunk,
                     std::vector<double>& scratch);
  void pushInput(const float* const* in, int n);
  void renderNativeBlock(const ParamValues& p);
  // A resumed converter came back out of step: never skip on this path until the next
  // prepare(), and restart every converter and FIFO of the wrapper together (a one-off gap),
  // so the output keeps the reported latency.
  void fallBackInput();
  void fallBackOutput();
  // reset() without resetting the engine or the extra delay: every converter, FIFO, the input
  // prime and the output pad, so the onset lands at the reported latency again.
  void restartConversion();
  void applyExtraDelay(float* const* out, int n);

  std::unique_ptr<VoiceEngine> inner_;
  ConverterQuality quality_ = ConverterQuality::Studio;
  ConverterSpec spec_;
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
  std::unique_ptr<r8b::CDSPResampler> resampler_[kMaxChannels];
  std::vector<float> native_[kMaxChannels];
  std::vector<double> scratch_;
  std::vector<float> fifo_[kMaxChannels];
  size_t fifoRead_ = 0;
  size_t fifoSize_ = 0;
  size_t zeroPad_ = 0;  // host samples of silence owed after reset()

  // Input path: host -> native.
  std::unique_ptr<r8b::CDSPResampler> inputResampler_[kMaxChannels];
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
  size_t fallbacks_ = 0;
  bool restartPending_ = false;  // an output fallback: restart at the next block

  // Alignment delay.
  int extra_ = 0;
  std::vector<float> delay_[kMaxChannels];
  size_t delayPos_ = 0;
};

}  // namespace eg13
