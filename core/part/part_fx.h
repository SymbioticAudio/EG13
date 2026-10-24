#pragma once

#include <cstdint>
#include <vector>

#include "engine/voice_engine.h"
#include "voice/processor_slot.h"
#include "voice/source_factory.h"

namespace eg13 {

// What goes into a part's Part FX (owner decision 2). Append-only: saved values keep their meaning.
enum class PartFxInput : int { PartMix = 0, Input = 1, Both = 2 };
constexpr int kNumPartFxInputs = 3;

// A part's Part FX slot (M5): Σ voices -> Part FX -> the part's level, pan and Mono.
//
// Engine 0 is Off. Off passes the part mix, delayed by the slot latency, and ignores the input
// selector, so it never leaks the Input. Every other engine gets the selected signals:
// - PartMix: the part mix.
// - Input: the part's Input; the part mix bypasses the effect through a delay line of the slot
//   latency, so it stays in time with it.
// - Both: the part mix plus the Input.
// The Input is delayed by the voices' latency before the effect, so it leaves the part at the same
// latency as the voices (provisional ruling 20).
//
// The selector's gains ramp over kSlotFadeSeconds (G12). The bypass gain follows the effect's
// input with the slot latency: the mix the effect stops receiving is the mix the bypass starts
// carrying when that mix reaches the output. On a type change the bypass moves with the slot's own
// crossfade (the slot latency's hold, then 10 ms).
//
// Controls (P's, the note and triggers) are given once per 16-sample tick by beginTick() and act
// after the voices' latency, so they line up with the sound made in that tick (ruling 20). When a
// new engine starts, the P's queued for the engine it replaced are not its own: the next tick's P's
// replace them (and act at once); queued notes and triggers keep their time.
class PartFx {
 public:
  explicit PartFx(std::vector<SourceSpec> engines);  // index = PartFxEngine; 0 must be Off

  // Main thread. voiceLatency: the voices' latency in host samples (the control and Input delay).
  void prepare(double hostRate, int maxBlock, int voiceLatency,
               ConverterQuality q = ConverterQuality::Studio);
  // Every engine, the delay lines and the controls (zero P's, note 60); no fade.
  void reset();

  // Audio thread (once per block, from the part's settings). Crossfades while sounding.
  void setEngine(int engine);
  int engine() const { return slot_.engine(); }  // the engine playing (fading in, during a fade)
  void setInput(PartFxInput mode);
  PartFxInput input() const { return mode_; }

  int latencySamples() const { return slot_.latencySamples(); }

  // One tick's controls: the P's (host values plus routes), the note, and whether a note started
  // in the tick. They act voiceLatency samples after the tick began: a trigger is one
  // noteOn(note, 1) followed by noteOff() (G26: a later type change does not retrigger the new
  // engine); a changed note is a setNote(). The first tick after reset() also sets the P's and the
  // note at once: nothing before it has reached the output.
  void beginTick(const ParamValues& p, float note, bool trigger);

  // mix: the part mix; input: the part's Input (nullable: silence); out: must not alias mix.
  // Two channels each, n <= maxBlock.
  void process(const float* const* mix, const float* const* input, float* const* out, int n);

  const ProcessorSlot& slot() const { return slot_; }  // tests
  const ParamValues& appliedParams() const { return params_; }  // tests: the P's acting now

 private:
  struct Tick {
    int64_t start = 0;  // time_ when the tick began
    ParamValues p;
    float note = 60.0f;
    bool trigger = false;
  };
  struct Ramp {  // linear, kSlotFadeSeconds from any value to the target
    float value = 0.0f;
    float target = 0.0f;
    float next(float step);
  };

  void apply(const Tick& tick);
  void applyDue();          // every stored tick whose time has come
  int64_t nextDue() const;  // when the oldest stored tick acts (INT64_MAX: none)
  void followSlot();        // a fade the slot started at the end of its last process() call
  float bypassOf(int engine, float effectMix) const;  // the bypass gain of one engine
  void render(const float* const* mix, const float* const* input, float* const* out, int offset,
              int n);

  ProcessorSlot slot_;
  PartFxInput mode_ = PartFxInput::PartMix;
  int voiceLatency_ = 0;
  float step_ = 1.0f;  // the selector ramps' and the slot fade's step per sample

  // The controls.
  std::vector<Tick> ticks_;  // a ring, voiceLatency / 16 + 2 long
  int tickOldest_ = 0;
  int tickCount_ = 0;
  int64_t time_ = 0;  // samples processed
  bool fresh_ = true;  // no tick since reset()
  bool retarget_ = false;  // an engine started: the next tick's P's replace every queued tick's
  bool idle_ = true;   // no process() since reset(): a selector change snaps
  ParamValues params_{};
  float note_ = 60.0f;

  // The selector: the effect's input gains.
  Ramp mixGain_;
  Ramp inputGain_;

  // The slot's crossfade, followed sample by sample (the slot's own arithmetic).
  int engine_ = 0;     // the slot's engine
  int previous_ = -1;  // the engine fading out; -1: none
  int fadeHold_ = 0;
  float fadeWeight_ = 1.0f;

  // Delay lines (allocated in prepare).
  std::vector<float> inputLine_[2];  // the Input, voiceLatency long
  int inputLinePos_ = 0;
  std::vector<float> bypassLine_[2];  // the part mix, latencySamples() long
  std::vector<float> mixGainLine_;    // the effect's mix gain, latencySamples() long
  int bypassLinePos_ = 0;
  // Scratch, maxBlock long.
  std::vector<float> in_[2];      // the effect's input
  std::vector<float> wet_[2];     // the slot's output
  std::vector<float> bypass_[2];  // the delayed mix
  std::vector<float> bypassGain_;  // the delayed mix gain
};

}  // namespace eg13
