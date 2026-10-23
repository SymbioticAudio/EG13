#pragma once

namespace eg13 {
namespace test {

// Code built with Rack's flags lives in an inline namespace rack_flags (see
// core/engine/ripples_model.h); this one is the tests' own, so it shares no symbol with the
// product copy in eg13::rack_flags.
inline namespace rack_flags {

// The test reference: the bare VCV Shelves model (AudibleInstruments src/Shelves/shelves.hpp),
// built in flagged_shelves.cpp with Rack's -funsafe-math-optimizations, as VCV ships it, so the
// adapter can be compared with it at 1e-5. Mirrors shelves::ShelvesEngine's interface.
class FlaggedShelves {
 public:
  struct Frame {  // shelves::ShelvesEngine::Frame
    float hs_freq_knob, hs_gain_knob, p1_freq_knob, p1_gain_knob, p1_q_knob;
    float p2_freq_knob, p2_gain_knob, p2_q_knob, ls_freq_knob, ls_gain_knob;
    float hs_freq_cv, hs_gain_cv, p1_freq_cv, p1_gain_cv, p1_q_cv, p2_freq_cv, p2_gain_cv, p2_q_cv;
    float ls_freq_cv, ls_gain_cv, global_freq_cv, global_gain_cv, main_in;
    bool hs_freq_cv_connected, hs_gain_cv_connected, p1_freq_cv_connected, p1_gain_cv_connected;
    bool p1_q_cv_connected, p2_freq_cv_connected, p2_gain_cv_connected, p2_q_cv_connected;
    bool ls_freq_cv_connected, ls_gain_cv_connected, global_freq_cv_connected,
        global_gain_cv_connected;
    float p1_hp_out, p1_bp_out, p1_lp_out, p2_hp_out, p2_bp_out, p2_lp_out, main_out;
    bool p1_hp_out_connected, p1_bp_out_connected, p1_lp_out_connected;
    bool p2_hp_out_connected, p2_bp_out_connected, p2_lp_out_connected;
    float clip;
    bool pre_gain;
  };

  FlaggedShelves();
  ~FlaggedShelves();
  FlaggedShelves(const FlaggedShelves&) = delete;
  FlaggedShelves& operator=(const FlaggedShelves&) = delete;

  void setSampleRate(float sampleRate);
  void process(Frame& frame);

 private:
  struct State;
  State* s_;  // owned; a raw pointer so no std:: template is instantiated on it
};

}  // namespace rack_flags
}  // namespace test
}  // namespace eg13
