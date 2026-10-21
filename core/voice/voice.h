#pragma once

#include <memory>
#include <vector>

#include "engine/native_rate_wrapper.h"
#include "voice/source_factory.h"

namespace eg13 {

// One synth voice: the source engine at its native rate, plus note, glide and
// bend handling and silence detection.
class Voice {
 public:
  static constexpr int kControlBlock = 16;  // pitch updates every 16 host samples

  enum class Alignment { Notes, Input };

  explicit Voice(SourceSpec source);                // one engine
  explicit Voice(std::vector<SourceSpec> sources);  // index = SourceEngine

  // main thread
  void prepare(double hostRate, int maxBlock, Alignment alignment = Alignment::Notes);
  void reset();
  void setEngine(int engine);  // audio thread
  int engine() const { return current_; }
  int engineCount() const { return int(sources_.size()); }
  void trigger(int key, float velocity);
  void glideTo(int key);
  void release();
  void setPitchOffset(float semitones) { pitchOffset_ = semitones; }
  void setGlideSeconds(float seconds) { glideSeconds_ = seconds; }

  // Adds n samples into out[0], out[1]. in: host-rate input (nullable).
  void process(const float* const* in, float* const* out, const ParamValues& p, int n);
  void process(float* const* out, const ParamValues& p, int n) { process(nullptr, out, p, n); }

  bool active() const { return active_; }
  bool gate() const { return gate_; }
  float currentNote() const { return float(currentNote_); }
  int latencySamples() const { return latency_; }

 private:
  bool blockIsFinite(int count) const;
  NativeRateWrapper& source() { return *sources_[size_t(current_)]; }

  std::vector<std::unique_ptr<NativeRateWrapper>> sources_;
  int current_ = 0;
  int latency_ = 0;
  double hostRate_ = 48000.0;
  float targetNote_ = 60.0f;
  double currentNote_ = 60.0;  // double: float stalls short of the target on slow glides
  float pitchOffset_ = 0.0f;
  float glideSeconds_ = 0.0f;
  bool gate_ = false;
  bool retrigger_ = false;  // re-send the note after a NaN reset
  float velocity_ = 1.0f;
  bool active_ = false;
  int silentSamples_ = 0;
  int silenceLimit_ = 2400;
  float buffer_[2][kControlBlock] = {};
};

}  // namespace eg13
