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

const char* const kExtInNames[ElementsEngine::kNumExtIn] = {"Off", "Blow", "Strike", "Both"};

void formatExtIn(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%s", kExtInNames[ElementsEngine::extInIndex(v)]);
}

// Defaults are Elements' power-on Patch (elements/dsp/part.cc, Part::Init); the firmware has
// no stored pot positions. Ext In defaults to Strike, the routing EG13 always had.
const EngineParamInfo kParams[16] = {
    {"Model", 0.5f / 3.0f, formatModel},
    {"Contour", 1.0f, formatPercent},
    {"Bow", 0.0f, formatPercent},
    {"Blow", 0.0f, formatPercent},
    {"Strike", 0.8f, formatPercent},
    {"Flow", 0.5f, formatPercent},
    {"Mallet", 0.5f, formatPercent},
    {"Bow Timbre", 0.5f, formatPercent},
    {"Blow Timbre", 0.5f, formatPercent},
    {"Strike Timbre", 0.5f, formatPercent},
    {"Geometry", 0.2f, formatPercent},
    {"Brightness", 0.5f, formatPercent},
    {"Damping", 0.25f, formatPercent},
    {"Position", 0.3f, formatPercent},
    {"Space", 0.5f, formatPercent},
    {"Ext In", 2.5f / 4.0f, formatExtIn},  // stepped quarters
};

const EngineInfo kInfo = {"elements", "Modal Synth", 16, kParams, true};

// Upper clamps from Elements' CvScaler::Read (elements/cv_scaler.cc) BIND calls. Strike
// Timbre's is 0.995, not 0.9995.
constexpr float kMaxKnob = 0.9995f;
constexpr float kMaxStrikeTimbre = 0.995f;

float knob(float v, float max = kMaxKnob) { return std::min(clamp01(v), max); }

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

int ElementsEngine::extInIndex(float normalized) {
  const int i = int(clamp01(normalized) * float(kNumExtIn));
  return i < kNumExtIn ? i : kNumExtIn - 1;
}

void ElementsEngine::prepare(double hostRate, int) {
  pitchOffset_ = float(12.0 * std::log2(kNativeRate / hostRate));
  reset();
}

// Audio-thread safe by design: memset (~114 KB) + reverb fill (64 KB), no allocation, ~3 us.
// Keep it that way.
void ElementsEngine::reset() {
  // Part::Init() leaves panic_ and easter_egg_ unset.
  std::memset(static_cast<void*>(&impl_->part), 0, sizeof(impl_->part));
  impl_->part.Init(impl_->reverb);
  uint32_t seed[3] = {1, 2, 3};
  impl_->part.Seed(seed, 3);
  std::memset(impl_->blow, 0, sizeof(impl_->blow));
  std::memset(impl_->strike, 0, sizeof(impl_->strike));
  gate_ = false;
  deliveredGate_ = false;
  pendingStrike_ = false;
  model_ = -1;
}

void ElementsEngine::noteOn(float note, float velocity) {
  note_ = note;
  velocity_ = clamp01(velocity);
  gate_ = true;
  pendingStrike_ = true;
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
  // Pot-only mapping of CvScaler::Read (CV term at 0): Contour and the three levels are the
  // linear pot as is; the rest are the linear pot clamped by BIND. Space's BIND range is 0..2,
  // but a pot alone reaches only 0..1 (freeze at >= 1.75 needs CV), so it maps 0..1 too.
  // CvScaler's per-control one-pole smoothing is left to the host's parameter smoothing.
  elements::Patch* patch = s.part.mutable_patch();
  patch->exciter_envelope_shape = clamp01(p.p[1]);
  patch->exciter_bow_level = clamp01(p.p[2]);
  patch->exciter_blow_level = clamp01(p.p[3]);
  patch->exciter_strike_level = clamp01(p.p[4]);
  patch->exciter_blow_meta = knob(p.p[5]);
  patch->exciter_strike_meta = knob(p.p[6]);
  patch->exciter_bow_timbre = knob(p.p[7]);
  patch->exciter_blow_timbre = knob(p.p[8]);
  patch->exciter_strike_timbre = knob(p.p[9], kMaxStrikeTimbre);
  patch->exciter_signature = 0.0f;
  patch->resonator_geometry = knob(p.p[10]);
  patch->resonator_brightness = knob(p.p[11]);
  patch->resonator_damping = knob(p.p[12]);
  patch->resonator_position = knob(p.p[13]);
  patch->space = clamp01(p.p[14]);

  elements::PerformanceState state;
  // Elements only re-excites on a gate rising edge: retriggers get one low block first.
  bool level = gate_;
  if (pendingStrike_ && deliveredGate_) {
    level = false;
  } else if (pendingStrike_) {
    level = true;
    pendingStrike_ = false;
  }
  deliveredGate_ = level;
  state.gate = level;
  state.note = note_ + pitchOffset_;
  state.modulation = 0.0f;
  state.strength = velocity_;

  // Ext In mirrors the two EXT IN jacks. Unpatched jacks read silence on the hardware (no
  // normalization), so a null input or an unrouted exciter gets zeros.
  const int route = extInIndex(p.p[15]);
  const bool toBlow = in && (route == 1 || route == 3);
  const bool toStrike = in && (route == 2 || route == 3);
  for (int i = 0; i < count; ++i) {
    const float x = in ? 0.5f * (in[0][offset + i] + in[1][offset + i]) : 0.0f;
    s.blow[i] = toBlow ? x : 0.0f;
    s.strike[i] = toStrike ? x : 0.0f;
  }
  s.part.Process(state, s.blow, s.strike, s.main, s.aux, size_t(count));
  for (int i = 0; i < count; ++i) {
    out[0][offset + i] = s.aux[i];
    out[1][offset + i] = s.main[i];
  }
}

}  // namespace eg13
