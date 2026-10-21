// Built with Rack's flags (-O3 -funsafe-math-optimizations, the eg13_vcv_flagged object library
// in core/CMakeLists.txt). The isolation rules for such a TU (preflight ruling F6), which every
// flagged VCV model TU follows (Shelves too):
//   1. Include, at file scope, only this TU's own header, the headers the model includes from the
//      standard library, and rack_prelude.h (standard and SSE headers, the shared random state).
//      Their include guards then keep them out of the wrapper below.
//   2. Include rack.hpp and the upstream model inside `namespace eg13 { inline namespace
//      rack_flags {`, so every inline function they emit out of line, Rack shim included, has
//      its own eg13::rack_flags:: symbol and cannot merge with an unflagged copy.
//   3. Declare the TU's API in that namespace too (ripples_model.h), and define nothing else.
//   4. Add the object to the symbol-check ctest ("flagged VCV models define symbols only in their
//      rack_flags namespace", scripts/check-flagged-symbols.sh). The library forces -O3 in every
//      configuration, so the standard-library templates the model instantiates (std::min, ...)
//      stay inlined; the ctest checks that too.
#include "engine/ripples_model.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "rack_prelude.h"

namespace eg13 {
inline namespace rack_flags {

#include "rack.hpp"
#include "Ripples/ripples.hpp"

struct RipplesModel::State {
  ripples::RipplesEngine engine;
};

RipplesModel::RipplesModel() : s_(new State()) {}

RipplesModel::~RipplesModel() { delete s_; }

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
