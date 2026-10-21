#pragma once

#include <memory>
#include <vector>

#include "engine/native_rate_wrapper.h"
#include "engine/stereo_link.h"
#include "voice/source_factory.h"

namespace eg13 {

constexpr double kSlotFadeSeconds = 0.01;  // crossfade between engines on a change

// The mono signals a Shaper's side inputs can select (SideSource), each nullable (silence).
// The slot routes them to each engine by that engine's own selectors, so an engine fading out
// keeps the side inputs its own settings chose.
struct SideSignals {
  const float* sourceAux = nullptr;  // SideSource::SourceAux
  const float* input = nullptr;      // SideSource::Input
  const float* otherPart = nullptr;  // SideSource::OtherPart
};

// One Filter or Shaper slot of a voice (spec §2). Holds every engine of the slot, preallocated,
// each in its own NativeRateWrapper and padded to one slot latency, so a change of engine never
// moves the output in time. Tells its engine whether the input is stereo (§5.1).
class ProcessorSlot {
 public:
  explicit ProcessorSlot(std::vector<SourceSpec> engines);  // index = engine id; at least one

  // Main thread: prepares every engine (allocates) and pads it to latencySamples().
  void prepare(double hostRate, int maxBlock);
  void reset();    // every engine; no fade; mono. A waiting request takes effect at once.
  void restart();  // the current engine only: a new note on an idle voice, or after a NaN.
                   // A waiting request takes effect at once, with no fade.

  int latencySamples() const { return latency_; }
  int engineCount() const { return int(engines_.size()); }
  int engine() const { return current_; }  // the engine playing (fading in, during a fade)
  int pendingEngine() const { return pending_; }  // a request waiting for the fade; -1: none
  const EngineInfo& info() const { return engines_[size_t(current_)]->info(); }
  bool stereo() const { return detector_.stereo(); }
  bool fading() const { return previous_ >= 0; }

  // Audio thread. The old engine keeps running with its last parameters, alone for the slot
  // latency (when the new engine's first output arrives), then crossfades to the new one over
  // kSlotFadeSeconds. A request during a fade waits until the fade ends, then starts its own
  // (the last request wins; asking for the engine fading in cancels it). While a request
  // waits, the engine fading in keeps the parameters it had: the p given to process() then
  // belong to the requested engine, as the host already shows them.
  void setEngine(int engine);

  void noteOn(float note, float velocity);
  void noteOff();
  void setNote(float note);

  // in: two channels; sides: the side signals (nullable: all silent); out: two channels, not
  // aliasing in. p: the parameters of the latest requested engine. n <= prepare()'s maxBlock.
  void process(const float* const* in, const SideSignals* sides, float* const* out,
               const ParamValues& p, int n);

 private:
  NativeRateWrapper& at(int engine) { return *engines_[size_t(engine)]; }
  void startFade(int engine, const ParamValues& outgoing);
  void render(int engine, const float* const* in, const SideSignals* sides, float* const* out,
              const ParamValues& p, bool stereo, int n);

  std::vector<std::unique_ptr<NativeRateWrapper>> engines_;
  int current_ = 0;
  int previous_ = -1;
  int pending_ = -1;
  int latency_ = 0;
  StereoDetector detector_;
  ParamValues last_{};        // the current engine's parameters from the last block
  ParamValues held_{};        // the current engine's, frozen while a request waits
  ParamValues fadeParams_{};  // the previous engine's, held through the fade
  int fadeHold_ = 0;
  float fadeWeight_ = 1.0f;
  float fadeStep_ = 1.0f;
  float note_ = 60.0f;
  float velocity_ = 1.0f;
  bool gate_ = false;
  std::vector<float> fade_[2];
};

}  // namespace eg13
