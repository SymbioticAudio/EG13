#pragma once

#include <memory>
#include <vector>

#include "engine/native_rate_wrapper.h"
#include "voice/processor_slot.h"
#include "voice/source_factory.h"

namespace eg13 {

// What a voice reads besides its notes: the part's Input as its Input source setting routes it
// (spec §2.3), and the other part's output (soft-limited by the Synth). The Shaper's side
// signals (the Ring Mod / Vocoder Carrier and Modulator) come from either, or from the Source's
// AUX.
struct VoiceInputs {
  const float* const* input = nullptr;  // two channels, nullable
  const float* const* other = nullptr;  // two channels, nullable
};

// Post-modulation parameters of each slot in the voice chain.
struct SlotParams {
  ParamValues source;
  ParamValues filter;
  ParamValues shaper;
};

// One synth voice: Source -> Filter -> Shaper (spec §2), plus note, glide and bend handling and
// silence detection.
class Voice {
 public:
  static constexpr int kControlBlock = 16;  // pitch updates every 16 host samples

  enum class Alignment { Notes, Input };

  explicit Voice(SourceSpec source);                // one source; Filter and Shaper only Off
  explicit Voice(std::vector<SourceSpec> sources);  // index = SourceEngine; Filter, Shaper Off
  Voice(std::vector<SourceSpec> sources, std::vector<SourceSpec> filters,
        std::vector<SourceSpec> shapers);  // index = SourceEngine / FilterEngine / ShaperEngine

  // main thread
  void prepare(double hostRate, int maxBlock, Alignment alignment = Alignment::Notes);
  void reset();
  void setEngine(int engine);        // Source; audio thread
  void setFilterEngine(int engine);  // audio thread; crossfades while sounding
  void setShaperEngine(int engine);  // audio thread; crossfades while sounding
  int engine() const { return current_; }
  int filterEngine() const { return filter_.engine(); }
  int shaperEngine() const { return shaper_.engine(); }
  int engineCount() const { return int(sources_.size()); }
  void trigger(int key, float velocity);
  void glideTo(int key);
  void release();
  void setPitchOffset(float semitones) { pitchOffset_ = semitones; }
  void setGlideSeconds(float seconds) { glideSeconds_ = seconds; }

  // Adds n samples into out[0], out[1].
  void process(const VoiceInputs& in, float* const* out, const SlotParams& p, int n);
  // Source parameters only (Filter and Shaper get zeros). in: the part's Input (nullable).
  void process(const float* const* in, float* const* out, const ParamValues& p, int n);
  void process(float* const* out, const ParamValues& p, int n) { process(nullptr, out, p, n); }

  bool active() const { return active_; }
  bool gate() const { return gate_; }
  float currentNote() const { return float(currentNote_); }
  int latencySamples() const { return latency_; }  // Source + Filter + Shaper

 private:
  // The stages of the chain, as bits: which ones failed, or wait for the held note again.
  static constexpr unsigned kSourceStage = 1u;
  static constexpr unsigned kFilterStage = 2u;
  static constexpr unsigned kShaperStage = 4u;
  static constexpr unsigned kAllStages = kSourceStage | kFilterStage | kShaperStage;

  // The stages whose output in this control block is not finite.
  unsigned failedStages(int count) const;
  // The signals the Shaper's side inputs can select for this control block: the Source AUX,
  // and the part's Input and the other part's output summed to mono (nullptr when absent).
  // The slot picks from them by each engine's own selectors.
  SideSignals sideSignals(const VoiceInputs& in, int offset, int count);
  void restart(unsigned stages);  // reset the engines playing in these stages
  void noteOn(unsigned stages);   // the held note, at the current pitch and velocity
  NativeRateWrapper& source() { return *sources_[size_t(current_)]; }

  std::vector<std::unique_ptr<NativeRateWrapper>> sources_;
  ProcessorSlot filter_;
  ProcessorSlot shaper_;
  int current_ = 0;
  int latency_ = 0;
  double hostRate_ = 48000.0;
  float targetNote_ = 60.0f;
  double currentNote_ = 60.0;  // double: float stalls short of the target on slow glides
  float pitchOffset_ = 0.0f;
  float glideSeconds_ = 0.0f;
  bool gate_ = false;
  unsigned retrigger_ = 0;  // stages to re-send the held note to after a NaN reset
  float velocity_ = 1.0f;
  bool active_ = false;
  int silentSamples_ = 0;
  int silenceLimit_ = 2400;
  float source_[3][kControlBlock] = {};  // Source L, R, AUX
  float filtered_[2][kControlBlock] = {};
  float side_[2][kControlBlock] = {};    // mono Input, mono other part
  float buffer_[2][kControlBlock] = {};  // the voice's output (Shaper)
};

}  // namespace eg13
