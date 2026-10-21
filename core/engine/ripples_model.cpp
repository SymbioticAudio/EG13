// Compiled with Rack's -funsafe-math-optimizations (core/CMakeLists.txt). Include nothing here
// beyond the model and what it needs: every symbol this TU defines must be inside
// eg13::rack_flags (ripples_model.h).
#include "engine/ripples_model.h"

// The headers the model includes, first, so their include guards keep them out of the wrapper.
#include <algorithm>
#include <cmath>
#include <random>

#include "rack.hpp"  // shared: Rack's random state is one per thread, as in Rack

namespace eg13 {
inline namespace rack_flags {

#include "Ripples/ripples.hpp"

struct RipplesModel::State {
  ripples::RipplesEngine engine;
};

RipplesModel::RipplesModel() : s_(std::make_unique<State>()) {}

RipplesModel::~RipplesModel() = default;

void RipplesModel::reset(float sampleRate) {
  // A fresh model clears every state, including the CV RC filters that setSampleRate() keeps
  // (ripples.hpp:145-165). VCV's onReset only calls setSampleRate (Ripples.cpp:58-67).
  s_->engine = ripples::RipplesEngine();
  s_->engine.setSampleRate(sampleRate);
}

void RipplesModel::copyFrom(const RipplesModel& other) { s_->engine = other.s_->engine; }

float RipplesModel::process(float freqKnob, float resKnob, float inputVolts, int output) {
  ripples::RipplesEngine::Frame frame = {};  // CV inputs unpatched, FM knob 0
  frame.freq_knob = freqKnob;
  frame.res_knob = resKnob;
  frame.input = inputVolts;
  s_->engine.process(frame);
  return output == kLP4 ? frame.lp4 : output == kLP2 ? frame.lp2 : frame.bp2;
}

}  // namespace rack_flags
}  // namespace eg13
