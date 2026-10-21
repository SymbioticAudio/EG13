// Compiled with Rack's -funsafe-math-optimizations (tests/CMakeLists.txt). Every symbol this TU
// defines must be inside eg13::test::rack_flags (flagged_ripples.h).
#include "reference/flagged_ripples.h"

// The headers the model includes, first, so their include guards keep them out of the wrapper.
#include <algorithm>
#include <cmath>
#include <random>

#include "rack.hpp"  // shared: Rack's random state, which the tests seed

namespace eg13 {
namespace test {
inline namespace rack_flags {

#include "Ripples/ripples.hpp"

struct FlaggedRipples::State {
  ripples::RipplesEngine engine;
};

FlaggedRipples::FlaggedRipples() : s_(std::make_unique<State>()) {}

FlaggedRipples::~FlaggedRipples() = default;

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
