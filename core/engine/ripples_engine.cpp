#include "engine/ripples_engine.h"

#include <cstdio>

#include "engine/param_format.h"

namespace eg13 {

namespace {

const char* const kModeNames[3] = {"LP4", "LP2", "BP2"};

void formatMode(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%s", kModeNames[RipplesEngine::modeIndex(v)]);
}

// Frequency: the VCV knob, 20 Hz .. 20 kHz exponential (Ripples.cpp:36,75; ripples.hpp:29-32),
// default fully open (Ripples.cpp:36). Resonance: 0..1, default 0 (Ripples.cpp:35). Mode picks
// one of the module's outputs, in the spec's order (LP4 / LP2 / BP2), which is RipplesModel's.
const EngineParamInfo kParams[3] = {
    {"Frequency", 1.0f, formatKnobHz},
    {"Resonance", 0.0f, formatPercent01},
    {"Mode", 0.5f / 3.0f, formatMode},
};

const EngineInfo kInfo = {"ripples", "Analog Filter", 3, kParams, true, false, 0, nullptr};

// Ripples' output amplifiers invert (ripples.hpp:95-97: kLP2Gain, kLP4Gain, kBP2Gain < 0). EG13
// restores the input's polarity so Dry and processed signals add rather than cancel.
float delivered(float volts) { return -volts / RipplesEngine::kVolts; }

}  // namespace

RipplesEngine::RipplesEngine() = default;

RipplesEngine::~RipplesEngine() = default;

const EngineInfo& RipplesEngine::staticInfo() { return kInfo; }

const EngineInfo& RipplesEngine::info() const { return kInfo; }

int RipplesEngine::modeIndex(float normalized) { return stepIndex(normalized, 3); }

float RipplesEngine::smoothedParam(int index) const {
  return index == 0 ? freq_.value() : index == 1 ? res_.value() : 0.0f;
}

void RipplesEngine::prepare(double hostRate, int maxBlock) {
  rate_ = hostRate;
  rightOut_.assign(size_t(maxBlock), 0.0f);
  link_.prepare(hostRate);
  reset();
}

void RipplesEngine::reset() {
  left_.reset(float(rate_));
  right_.copyFrom(left_);
  freq_.reset();
  res_.reset();
  link_.reset();
}

void RipplesEngine::process(const float* const* in, float* const* out, const ParamValues& p,
                            int n) {
  // The VCV module reads its knobs every sample (Ripples.cpp:74-76) and the model low-passes
  // them like the hardware's CV amplifiers (ripples.hpp:155-161). EG13's parameters change at
  // block boundaries, so they ramp across each block instead of stepping. ParamRamp takes
  // clamped targets.
  freq_.begin(clamp01(p.p[0]), n);
  res_.begin(clamp01(p.p[1]), n);
  const int mode = modeIndex(p.p[2]);
  if (link_.request(stereo_, true) == StereoLink::Start::CopyLeft) right_.copyFrom(left_);
  const bool right = link_.rightRuns();
  for (int i = 0; i < n; ++i) {
    const float f = freq_.at(i);
    const float r = res_.at(i);
    out[0][i] = delivered(left_.process(f, r, (in != nullptr ? in[0][i] : 0.0f) * kVolts, mode));
    if (right) {
      rightOut_[size_t(i)] =
          delivered(right_.process(f, r, (in != nullptr ? in[1][i] : 0.0f) * kVolts, mode));
    }
  }
  link_.mix(out[0], rightOut_.data(), out[1], n);
}

}  // namespace eg13
