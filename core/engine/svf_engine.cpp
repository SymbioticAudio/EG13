#include "engine/svf_engine.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <vector>

#include "engine/param_format.h"
#include "stmlib/dsp/filter.h"

namespace eg13 {

namespace {

const char* const kModeNames[3] = {"LP", "BP", "HP"};

void formatMode(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%s", kModeNames[SvfEngine::modeIndex(v)]);
}

// Resonance default: Q = 1/sqrt(2), a flat (Butterworth) passband: 0.5 * 80^p = 0.7071.
const float kDefaultResonance = float(std::log(2.0 * 0.70710678118654752) / std::log(80.0));

// Cutoff: the Analog Filter's curve, fully open by default. Resonance: Q 0.5..40, Shelves'
// Q knob range (AudibleInstruments shelves.hpp:31-32). Mode: LP / BP / HP (spec §4); BP is
// stmlib's normalized band-pass, unity at the cutoff (filter.h:243-244).
const EngineParamInfo kParams[3] = {
    {"Cutoff", 1.0f, formatKnobHz},
    {"Resonance", kDefaultResonance, formatKnobQ},
    {"Mode", 0.5f / 3.0f, formatMode},
};

const EngineInfo kInfo = {"svf", "State Variable Filter", 3, kParams, true, false, 0, nullptr};

float run(stmlib::Svf& f, int mode, float x) {
  if (mode == 0) return f.Process<stmlib::FILTER_MODE_LOW_PASS>(x);
  if (mode == 1) return f.Process<stmlib::FILTER_MODE_BAND_PASS_NORMALIZED>(x);
  return f.Process<stmlib::FILTER_MODE_HIGH_PASS>(x);
}

// stmlib's Svf deletes assignment and keeps its two state variables private, so the exact state
// copy a starting right instance needs (spec §5.1) copies the object's bytes. The assert stops
// the build if a stmlib bump changes the layout (five floats, nothing else) this relies on.
static_assert(std::is_standard_layout_v<stmlib::Svf> && sizeof(stmlib::Svf) == 5 * sizeof(float),
              "stmlib::Svf layout changed: copyFilter() must be revisited");
void copyFilter(stmlib::Svf& to, const stmlib::Svf& from) {
  std::memcpy(static_cast<void*>(&to), static_cast<const void*>(&from), sizeof(stmlib::Svf));
}

}  // namespace

struct SvfEngine::Impl {
  stmlib::Svf left;
  stmlib::Svf right;
  std::vector<float> rightOut;
};

SvfEngine::SvfEngine() : impl_(std::make_unique<Impl>()) {}

SvfEngine::~SvfEngine() = default;

const EngineInfo& SvfEngine::staticInfo() { return kInfo; }

const EngineInfo& SvfEngine::info() const { return kInfo; }

float SvfEngine::resonanceQ(float normalized) { return knobQ(normalized); }

int SvfEngine::modeIndex(float normalized) { return stepIndex(normalized, 3); }

float SvfEngine::smoothedParam(int index) const { return index == 0 ? f_.value() : q_.value(); }

void SvfEngine::prepare(double hostRate, int maxBlock) {
  rate_ = hostRate;
  impl_->rightOut.assign(size_t(maxBlock), 0.0f);
  link_.prepare(hostRate);
  reset();
}

void SvfEngine::reset() {
  impl_->left.Init();
  impl_->right.Init();
  f_.reset();
  q_.reset();
  link_.reset();
}

void SvfEngine::process(const float* const* in, float* const* out, const ParamValues& p, int n) {
  Impl& s = *impl_;
  // Frequency and Q ramp linearly across the block and set the filter every sample, as Plaits'
  // noise engine does (noise_engine.cc:76-91).
  f_.begin(std::min(knobHz(p.p[0]) / float(rate_), kMaxF), n);
  q_.begin(resonanceQ(p.p[1]), n);
  const int mode = modeIndex(p.p[2]);
  if (link_.request(stereo_, true) == StereoLink::Start::CopyLeft) copyFilter(s.right, s.left);
  const bool right = link_.rightRuns();
  for (int i = 0; i < n; ++i) {
    s.left.set_f_q<stmlib::FREQUENCY_ACCURATE>(f_.at(i), q_.at(i));
    out[0][i] = run(s.left, mode, in != nullptr ? in[0][i] : 0.0f);
    if (right) {
      s.right.set(s.left);
      s.rightOut[size_t(i)] = run(s.right, mode, in != nullptr ? in[1][i] : 0.0f);
    }
  }
  link_.mix(out[0], s.rightOut.data(), out[1], n);
}

}  // namespace eg13
