// Built with Rack's flags (-O3 -funsafe-math-optimizations, the eg13_vcv_flagged object library
// in core/CMakeLists.txt), following the isolation rules at the top of ripples_model.cpp.
#include "engine/shelves_model.h"

#include <algorithm>
#include <cmath>

#include "rack_prelude.h"

namespace eg13 {
inline namespace rack_flags {

#include "rack.hpp"
#include "Shelves/shelves.hpp"

struct ShelvesModel::State {
  shelves::ShelvesEngine engine;
};

ShelvesModel::ShelvesModel() : s_(new State()) {}

ShelvesModel::~ShelvesModel() { delete s_; }

void ShelvesModel::reset(float sampleRate) {
  // setSampleRate() alone already clears every filter state (shelves.hpp:281-311), as VCV's
  // onReset relies on (Shelves.cpp:106-116); starting from a fresh model as well keeps reset()
  // the same as RipplesModel's.
  s_->engine = shelves::ShelvesEngine();
  s_->engine.setSampleRate(sampleRate);
}

void ShelvesModel::copyFrom(const ShelvesModel& other) { s_->engine = other.s_->engine; }

float ShelvesModel::process(const Knobs& knobs, bool preGain, float inputVolts, int output) {
  shelves::ShelvesEngine::Frame frame = {};  // CV inputs unpatched
  frame.hs_freq_knob = knobs.hsFreq;
  frame.hs_gain_knob = knobs.hsGain;
  frame.p1_freq_knob = knobs.p1Freq;
  frame.p1_gain_knob = knobs.p1Gain;
  frame.p1_q_knob = knobs.p1Q;
  frame.p2_freq_knob = knobs.p2Freq;
  frame.p2_gain_knob = knobs.p2Gain;
  frame.p2_q_knob = knobs.p2Q;
  frame.ls_freq_knob = knobs.lsFreq;
  frame.ls_gain_knob = knobs.lsGain;
  frame.main_in = inputVolts;
  frame.pre_gain = preGain;
  // Mid 2's taps are only computed while one of them is patched (shelves.hpp:417-420,487-492),
  // so they are "patched" exactly while one of them is the chosen output.
  frame.p2_lp_out_connected = frame.p2_bp_out_connected = frame.p2_hp_out_connected =
      output >= kMid2LP;
  s_->engine.process(frame);
  switch (output) {
    case kMid1LP: return frame.p1_lp_out;
    case kMid1BP: return frame.p1_bp_out;
    case kMid1HP: return frame.p1_hp_out;
    case kMid2LP: return frame.p2_lp_out;
    case kMid2BP: return frame.p2_bp_out;
    case kMid2HP: return frame.p2_hp_out;
    default: return frame.main_out;
  }
}

}  // namespace rack_flags
}  // namespace eg13
