// Built with Rack's flags (eg13_vcv_flagged_reference, tests/CMakeLists.txt), following the
// isolation rules at the top of core/engine/ripples_model.cpp, in eg13::test::rack_flags.
#include "reference/flagged_shelves.h"

#include <algorithm>
#include <cmath>

#include "rack_prelude.h"  // shared: Rack's random state

namespace eg13 {
namespace test {
inline namespace rack_flags {

#include "rack.hpp"
#include "Shelves/shelves.hpp"

struct FlaggedShelves::State {
  shelves::ShelvesEngine engine;
};

FlaggedShelves::FlaggedShelves() : s_(new State()) {}

FlaggedShelves::~FlaggedShelves() { delete s_; }

void FlaggedShelves::setSampleRate(float sampleRate) { s_->engine.setSampleRate(sampleRate); }

void FlaggedShelves::process(Frame& f) {
  shelves::ShelvesEngine::Frame frame = {};
  frame.hs_freq_knob = f.hs_freq_knob;
  frame.hs_gain_knob = f.hs_gain_knob;
  frame.p1_freq_knob = f.p1_freq_knob;
  frame.p1_gain_knob = f.p1_gain_knob;
  frame.p1_q_knob = f.p1_q_knob;
  frame.p2_freq_knob = f.p2_freq_knob;
  frame.p2_gain_knob = f.p2_gain_knob;
  frame.p2_q_knob = f.p2_q_knob;
  frame.ls_freq_knob = f.ls_freq_knob;
  frame.ls_gain_knob = f.ls_gain_knob;
  frame.hs_freq_cv = f.hs_freq_cv;
  frame.hs_gain_cv = f.hs_gain_cv;
  frame.p1_freq_cv = f.p1_freq_cv;
  frame.p1_gain_cv = f.p1_gain_cv;
  frame.p1_q_cv = f.p1_q_cv;
  frame.p2_freq_cv = f.p2_freq_cv;
  frame.p2_gain_cv = f.p2_gain_cv;
  frame.p2_q_cv = f.p2_q_cv;
  frame.ls_freq_cv = f.ls_freq_cv;
  frame.ls_gain_cv = f.ls_gain_cv;
  frame.global_freq_cv = f.global_freq_cv;
  frame.global_gain_cv = f.global_gain_cv;
  frame.main_in = f.main_in;
  frame.hs_freq_cv_connected = f.hs_freq_cv_connected;
  frame.hs_gain_cv_connected = f.hs_gain_cv_connected;
  frame.p1_freq_cv_connected = f.p1_freq_cv_connected;
  frame.p1_gain_cv_connected = f.p1_gain_cv_connected;
  frame.p1_q_cv_connected = f.p1_q_cv_connected;
  frame.p2_freq_cv_connected = f.p2_freq_cv_connected;
  frame.p2_gain_cv_connected = f.p2_gain_cv_connected;
  frame.p2_q_cv_connected = f.p2_q_cv_connected;
  frame.ls_freq_cv_connected = f.ls_freq_cv_connected;
  frame.ls_gain_cv_connected = f.ls_gain_cv_connected;
  frame.global_freq_cv_connected = f.global_freq_cv_connected;
  frame.global_gain_cv_connected = f.global_gain_cv_connected;
  // The model writes Mid 2's taps only while one is connected (shelves.hpp:487-492).
  frame.p2_hp_out = f.p2_hp_out;
  frame.p2_bp_out = f.p2_bp_out;
  frame.p2_lp_out = f.p2_lp_out;
  frame.p1_hp_out_connected = f.p1_hp_out_connected;
  frame.p1_bp_out_connected = f.p1_bp_out_connected;
  frame.p1_lp_out_connected = f.p1_lp_out_connected;
  frame.p2_hp_out_connected = f.p2_hp_out_connected;
  frame.p2_bp_out_connected = f.p2_bp_out_connected;
  frame.p2_lp_out_connected = f.p2_lp_out_connected;
  frame.pre_gain = f.pre_gain;
  s_->engine.process(frame);
  f.p1_hp_out = frame.p1_hp_out;
  f.p1_bp_out = frame.p1_bp_out;
  f.p1_lp_out = frame.p1_lp_out;
  f.p2_hp_out = frame.p2_hp_out;
  f.p2_bp_out = frame.p2_bp_out;
  f.p2_lp_out = frame.p2_lp_out;
  f.main_out = frame.main_out;
  f.clip = frame.clip;
}

}  // namespace rack_flags
}  // namespace test
}  // namespace eg13
