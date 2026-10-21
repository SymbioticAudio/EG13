#pragma once

namespace eg13 {

// Built with Rack's flags, in the inline namespace eg13::rack_flags only: see ripples_model.h.
inline namespace rack_flags {

// The VCV Shelves model (AudibleInstruments src/Shelves/shelves.hpp, GPL-3.0-or-later), compiled
// alone in shelves_model.cpp.
class ShelvesModel {
 public:
  // What process() returns: the main output or one of the six filter taps, in EG13's order.
  enum Output { kEq = 0, kMid1LP, kMid1BP, kMid1HP, kMid2LP, kMid2BP, kMid2HP };

  // The module's ten knobs as the model takes them (shelves.hpp:214-223): frequencies and Qs
  // 0..1, gains -1..1.
  struct Knobs {
    float hsFreq, hsGain, p1Freq, p1Gain, p1Q, p2Freq, p2Gain, p2Q, lsFreq, lsGain;
  };

  ShelvesModel();  // allocates
  ~ShelvesModel();
  ShelvesModel(const ShelvesModel&) = delete;
  ShelvesModel& operator=(const ShelvesModel&) = delete;

  // Both audio-thread safe: no allocation.
  void reset(float sampleRate);              // a fresh model: every state cleared
  void copyFrom(const ShelvesModel& other);  // the other model's exact state

  // One sample: the knobs, the -6 dB pre-gain option and the input in volts, with the CV inputs
  // unpatched. Returns the chosen output in volts, as the module's jack gives it.
  float process(const Knobs& knobs, bool preGain, float inputVolts, int output);

 private:
  struct State;
  State* s_;  // owned; a raw pointer so no std:: template is instantiated on it
};

}  // namespace rack_flags
}  // namespace eg13
