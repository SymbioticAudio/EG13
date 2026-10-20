#include "engine/elements_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "elements/dsp/part.h"

namespace eg13 {

namespace {

const char* const kModelNames[ElementsEngine::kNumModels] = {"Modal", "String", "Strings"};

void formatModel(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%s", kModelNames[ElementsEngine::modelIndex(v)]);
}
void formatPercent(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%d%%", int(std::lround(clamp01(v) * 100.0f)));
}

// Defaults follow Elements' Patch initialisation (elements/dsp/part.cc).
const EngineParamInfo kParams[8] = {
    {"Model", 0.5f / 3.0f, formatModel},
    {"Bow", 0.0f, formatPercent},
    {"Blow", 0.0f, formatPercent},
    {"Strike", 0.8f, formatPercent},
    {"Geometry", 0.2f, formatPercent},
    {"Brightness", 0.5f, formatPercent},
    {"Damping", 0.25f, formatPercent},
    {"Position", 0.3f, formatPercent},
};

const EngineInfo kInfo = {"elements", "Modal Synth", 8, kParams, true};

constexpr float kMaxKnob = 0.9995f;  // Elements' own clamp for resonator controls

}  // namespace

struct ElementsEngine::Impl {
  elements::Part part;
  uint16_t reverb[32768];
  float blow[kNativeBlock];
  float strike[kNativeBlock];
  float main[kNativeBlock];
  float aux[kNativeBlock];
};

ElementsEngine::ElementsEngine() : impl_(std::make_unique<Impl>()) { reset(); }

ElementsEngine::~ElementsEngine() = default;

const EngineInfo& ElementsEngine::staticInfo() { return kInfo; }

const EngineInfo& ElementsEngine::info() const { return kInfo; }

int ElementsEngine::modelIndex(float normalized) {
  const int i = int(clamp01(normalized) * float(kNumModels));
  return i < kNumModels ? i : kNumModels - 1;
}

void ElementsEngine::prepare(double hostRate, int) {
  pitchOffset_ = float(12.0 * std::log2(kNativeRate / hostRate));
  reset();
}

void ElementsEngine::reset() {
  // Part::Init() leaves panic_ and easter_egg_ unset.
  std::memset(static_cast<void*>(&impl_->part), 0, sizeof(impl_->part));
  impl_->part.Init(impl_->reverb);
  uint32_t seed[3] = {1, 2, 3};
  impl_->part.Seed(seed, 3);
  std::memset(impl_->blow, 0, sizeof(impl_->blow));
  gate_ = false;
  model_ = -1;
}

void ElementsEngine::noteOn(float note, float velocity) {
  note_ = note;
  velocity_ = clamp01(velocity);
  gate_ = true;
}

void ElementsEngine::noteOff() { gate_ = false; }

void ElementsEngine::process(const float* const* in, float* const* out, const ParamValues& p,
                             int n) {
  for (int done = 0; done < n;) {
    const int count = std::min(kNativeBlock, n - done);
    renderBlock(in, p, out, done, count);
    done += count;
  }
}

void ElementsEngine::renderBlock(const float* const* in, const ParamValues& p, float* const* out,
                                 int offset, int count) {
  Impl& s = *impl_;
  const int model = modelIndex(p.p[0]);
  if (model != model_) {
    s.part.set_resonator_model(elements::ResonatorModel(model));
    model_ = model;
  }
  elements::Patch* patch = s.part.mutable_patch();
  patch->exciter_envelope_shape = 1.0f;
  patch->exciter_bow_level = clamp01(p.p[1]);
  patch->exciter_bow_timbre = 0.5f;
  patch->exciter_blow_level = clamp01(p.p[2]);
  patch->exciter_blow_meta = 0.5f;
  patch->exciter_blow_timbre = 0.5f;
  patch->exciter_strike_level = clamp01(p.p[3]);
  patch->exciter_strike_meta = 0.5f;
  patch->exciter_strike_timbre = 0.5f;
  patch->exciter_signature = 0.0f;
  patch->resonator_geometry = std::min(clamp01(p.p[4]), kMaxKnob);
  patch->resonator_brightness = std::min(clamp01(p.p[5]), kMaxKnob);
  patch->resonator_damping = std::min(clamp01(p.p[6]), kMaxKnob);
  patch->resonator_position = std::min(clamp01(p.p[7]), kMaxKnob);
  patch->space = 0.5f;

  elements::PerformanceState state;
  state.gate = gate_;
  state.note = note_ + pitchOffset_;
  state.modulation = 0.0f;
  state.strength = velocity_;

  for (int i = 0; i < count; ++i) {
    s.strike[i] = in ? 0.5f * (in[0][offset + i] + in[1][offset + i]) : 0.0f;
  }
  s.part.Process(state, s.blow, s.strike, s.main, s.aux, size_t(count));
  for (int i = 0; i < count; ++i) {
    out[0][offset + i] = s.aux[i];
    out[1][offset + i] = s.main[i];
  }
}

}  // namespace eg13
