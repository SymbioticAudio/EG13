#pragma once

#include <memory>

namespace eg13 {

// Code compiled with Rack's flags (-funsafe-math-optimizations, core/CMakeLists.txt) lives in the
// inline namespace eg13::rack_flags, and only there: the flagged TUs wrap the upstream models in
// it, so their out-of-line copies of the models' inline functions have their own symbols, and the
// linker can never swap them with the copies built without those flags (the tests' reference
// models) or hand a flagged copy of a shared helper to a bit-exact engine (preflight ruling F6).
// The ctest "flagged VCV models define symbols only in their rack_flags namespace" checks this.
inline namespace rack_flags {

// The VCV Ripples model (AudibleInstruments src/Ripples/ripples.hpp, GPL-3.0-or-later), compiled
// alone in ripples_model.cpp.
class RipplesModel {
 public:
  enum Output { kLP4 = 0, kLP2 = 1, kBP2 = 2 };

  RipplesModel();  // allocates
  ~RipplesModel();
  RipplesModel(const RipplesModel&) = delete;
  RipplesModel& operator=(const RipplesModel&) = delete;

  // Both audio-thread safe: no allocation.
  void reset(float sampleRate);              // a fresh model: every state cleared
  void copyFrom(const RipplesModel& other);  // the other model's exact state

  // One sample: the knobs (0..1) and the input in volts, with the CV inputs unpatched and the FM
  // knob at 0. Returns the chosen output in volts, as the module's jack gives it (inverted).
  float process(float freqKnob, float resKnob, float inputVolts, int output);

 private:
  struct State;
  std::unique_ptr<State> s_;
};

}  // namespace rack_flags
}  // namespace eg13
