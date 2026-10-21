#pragma once

namespace eg13 {
namespace test {

// Code built with Rack's flags lives in an inline namespace rack_flags (see
// core/engine/ripples_model.h); this one is the tests' own, so it shares no symbol with the
// product copy in eg13::rack_flags.
inline namespace rack_flags {

// The test reference: the bare VCV Ripples model (AudibleInstruments src/Ripples/ripples.hpp),
// built in flagged_ripples.cpp with Rack's -funsafe-math-optimizations, as VCV ships it, so the
// adapter can be compared with it at 1e-5. Mirrors ripples::RipplesEngine's interface.
class FlaggedRipples {
 public:
  struct Frame {  // ripples::RipplesEngine::Frame
    float res_knob, freq_knob, fm_knob;
    float res_cv, freq_cv, fm_cv, input, gain_cv;
    bool gain_cv_present;
    float bp2, lp2, lp4, lp4vca;
  };

  FlaggedRipples();
  ~FlaggedRipples();
  FlaggedRipples(const FlaggedRipples&) = delete;
  FlaggedRipples& operator=(const FlaggedRipples&) = delete;

  void setSampleRate(float sampleRate);
  void process(Frame& frame);

 private:
  struct State;
  State* s_;  // owned; a raw pointer so no std:: template is instantiated on it
};

}  // namespace rack_flags
}  // namespace test
}  // namespace eg13
