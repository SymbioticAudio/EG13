#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "engine/native_rate_wrapper.h"
#include "mod/voice_control.h"
#include "voice/processor_slot.h"
#include "voice/slot_params.h"
#include "voice/source_factory.h"

namespace eg13 {

// What a voice reads besides its notes: the part's Input as its Input source setting routes it
// (spec §2.3), and the other part's output (soft-limited by the Synth). The Shaper's side
// signals (the Ring Mod / Vocoder Carrier and Modulator) come from either, or from the Source's
// AUX.
struct VoiceInputs {
  const float* const* input = nullptr;  // two channels, nullable: the Source's input
  const float* const* other = nullptr;  // two channels, nullable
  // The Input as the Shaper's side signals see it: mono, delayed by sourceLatencySamples() so it
  // reaches the Shaper with the voice. Part fills it from input; nullable (silence).
  const float* inputSide = nullptr;
};

// One synth voice: Source -> Filter -> Shaper -> Amp -> pan (spec §2), plus note, glide and bend
// handling and silence detection.
class Voice {
 public:
  static constexpr int kControlBlock = 16;  // pitch updates every 16 host samples
  static constexpr double kAmpRampSeconds = 0.005;  // the gate-following Amp's ramps (spec §5.2)

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

  // M4 (Part's modulated path). beginTick(): the matrix's controls for the next `length`
  // samples (the rest of the Part's 16-sample tick). render(): adds n samples (n <= what is left
  // of the tick) of Source -> Filter -> Shaper with the tick's slot parameters and pitch, the
  // Source gain before the Filter, then the Amp (routed, or following the gate with 5 ms ramps),
  // the pan and the Voice Gain. Amp, pan and gains move in straight lines across the tick; on a
  // note's first tick from silence the pan and gains start at their values (no glide).
  // Each control is delayed to line up with the audio it acts on (fix round 1 ruling): the
  // Source's parameters and the pitch act at once, the Source gain and the Filter's and Shaper's
  // parameters after the Source path's latency, and the Amp, pan and Voice Gain (and the gate the
  // Amp follows) after latencySamples().
  void beginTick(const VoiceControl& control, int length);
  void render(const VoiceInputs& in, float* const* out, int n);
  float ampLevel() const { return appliedAmp_; }  // tests: the Amp on the last sample rendered

  // M1-M3 path (tests, and parts without modulation): no Amp, pan or gains. Adds n samples into
  // out[0], out[1].
  void process(const VoiceInputs& in, float* const* out, const SlotParams& p, int n);
  // Source parameters only (Filter and Shaper get zeros). in: the part's Input (nullable).
  void process(const float* const* in, float* const* out, const ParamValues& p, int n);
  void process(float* const* out, const ParamValues& p, int n) { process(nullptr, out, p, n); }

  bool active() const { return active_; }
  bool gate() const { return gate_; }
  float currentNote() const { return float(currentNote_); }
  int latencySamples() const { return latency_; }  // Source + Filter + Shaper
  int sourceLatencySamples() const { return sourceLatency_; }  // the Source path, as aligned

 private:
  // The stages of the chain, as bits: which ones failed, or wait for the held note again.
  static constexpr unsigned kSourceStage = 1u;
  static constexpr unsigned kFilterStage = 2u;
  static constexpr unsigned kShaperStage = 4u;
  static constexpr unsigned kAllStages = kSourceStage | kFilterStage | kShaperStage;

  // The stages whose output in this control block is not finite.
  unsigned failedStages(int count) const;
  // The signals the Shaper's side inputs can select for this control block: the Source AUX,
  // the aligned mono Input, and the other part's output summed to mono (nullptr when absent).
  // The slot picks from them by each engine's own selectors.
  SideSignals sideSignals(const VoiceInputs& in, int offset, int count);
  void restart(unsigned stages);  // reset the engines playing in these stages
  // The chain for n samples; c == nullptr is the M1-M3 path.
  void run(const VoiceInputs& in, float* const* out, const SlotParams& p, const VoiceControl* c, int n);
  float tickPosition(int sample) const;  // 0..1 across the tick, after `sample` more samples
  // The Amp, pan and Voice Gain one sample further into the tick (t: tickPosition), as the
  // controls give them now; the caller delays them by latencySamples().
  void stepAmp(const VoiceControl& c, float t);
  // The slot parameters of the tick that was current `delay` samples ago; returns how many
  // samples from now until that changes (a large number when no later tick is stored).
  int delayedParams(int delay, const SlotParams** params) const;
  // Fills the delay lines and the tick ring with these values: nothing stale plays after a reset
  // or into a note from silence.
  void snapDelays(float amp, float gain, float pan, float sourceGain, const SlotParams& params);
  void noteOn(unsigned stages);   // the held note, at the current pitch and velocity
  NativeRateWrapper& source() { return *sources_[size_t(current_)]; }

  std::vector<std::unique_ptr<NativeRateWrapper>> sources_;
  ProcessorSlot filter_;
  ProcessorSlot shaper_;
  int current_ = 0;
  int sourceLatency_ = 0;
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
  int silentSamples_ = 0;  // the Shaper's input silent since the release
  int outSilent_ = 0;      // the output after the Amp silent since the release (M4 path)
  int silenceLimit_ = 2400;
  // The M4 path's tick: controls, position, and the Amp, pan and gains at its start and now.
  VoiceControl control_;
  int tickLength_ = kControlBlock;
  int tickPos_ = 0;
  bool fresh_ = true;  // the next tick is the note's first from silence: snap pan and gains
  float ampStep_ = 1.0f / 240.0f;
  float amp_ = 0.0f;
  float ampFrom_ = 0.0f;
  float pan_ = 0.0f;
  float panFrom_ = 0.0f;
  float gain_ = 1.0f;
  float gainFrom_ = 1.0f;
  float sourceGain_ = 1.0f;
  float sourceGainFrom_ = 1.0f;
  // Delay lines and the tick ring that line the controls up with the audio (allocated in prepare).
  struct AmpState {
    float amp = 0.0f;
    float gain = 1.0f;
    float pan = 0.0f;
  };
  struct TickParams {
    int64_t start = 0;  // time_ when the tick began
    SlotParams params;
  };
  std::vector<AmpState> ampLine_;      // latencySamples() long
  int ampLinePos_ = 0;
  std::vector<float> sourceGainLine_;  // sourceLatencySamples() long
  int sourceGainLinePos_ = 0;
  std::vector<TickParams> ticks_;      // a ring of the last ticks' parameters
  int tickNewest_ = 0;
  int tickCount_ = 0;
  int filterDelay_ = 0;  // samples from the Source's input to the Filter's
  int shaperDelay_ = 0;  // ... and to the Shaper's
  int64_t time_ = 0;     // samples rendered on the M4 path
  float appliedAmp_ = 0.0f;
  float source_[3][kControlBlock] = {};  // Source L, R, AUX
  float filtered_[2][kControlBlock] = {};
  float side_[kControlBlock] = {};       // mono other part
  float buffer_[2][kControlBlock] = {};  // the voice's output (Shaper)
};

}  // namespace eg13
