// Built with Rack's flags (eg13_vcv_flagged_reference, tests/CMakeLists.txt), following the
// isolation rules at the top of core/engine/ripples_model.cpp, in eg13::test::rack_flags.
#include "reference/flagged_ripples.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "rack_prelude.h"  // shared: Rack's random state, which the tests seed

namespace eg13 {
namespace test {
inline namespace rack_flags {

#include "rack.hpp"
#include "Ripples/ripples.hpp"

struct FlaggedRipples::State {
  ripples::RipplesEngine engine;
};

FlaggedRipples::FlaggedRipples() : s_(new State()) {}

FlaggedRipples::~FlaggedRipples() { delete s_; }

void FlaggedRipples::setSampleRate(float sampleRate) { s_->engine.setSampleRate(sampleRate); }

void FlaggedRipples::process(Frame& f) {
  ripples::RipplesEngine::Frame frame = {};
  frame.res_knob = f.res_knob;
  frame.freq_knob = f.freq_knob;
  frame.fm_knob = f.fm_knob;
  frame.res_cv = f.res_cv;
  frame.freq_cv = f.freq_cv;
  frame.fm_cv = f.fm_cv;
  frame.input = f.input;
  frame.gain_cv = f.gain_cv;
  frame.gain_cv_present = f.gain_cv_present;
  s_->engine.process(frame);
  f.bp2 = frame.bp2;
  f.lp2 = frame.lp2;
  f.lp4 = frame.lp4;
  f.lp4vca = frame.lp4vca;
}

}  // namespace rack_flags
}  // namespace test
}  // namespace eg13
