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

// Firmware control smoothing, applied once per 16-sample block at 32 kHz like CvScaler::Read,
// which runs once per audio block (elements.cc:84, codec.h:99 kAudioChunkSize = 16):
// - every pot: pot_lp += 0.01 * (pot - pot_lp) (cv_scaler.cc:148-150);
// - then BIND clamps and, for lp_coefficient != 1, adds a second one-pole
//   (cv_scaler.cc:153-165, coefficients at :177-186).
// The pot scanner's multiplexing (one pot sampled every 2 * POT_LAST blocks) is not modelled.
constexpr float kPotLp = 0.01f;

struct Control {
  float bindLp;  // BIND's lp_coefficient; 1 means no second filter
  float max;     // BIND's upper clamp; 1 for the unbound pots (Contour and the levels)
};

const Control kControls[16] = {
    {1.0f, 1.0f},                // Model: stepped, not smoothed
    {1.0f, 1.0f},                // Contour       cv_scaler.cc:172
    {1.0f, 1.0f},                // Bow           :173
    {1.0f, 1.0f},                // Blow          :174
    {1.0f, 1.0f},                // Strike        :175
    {0.05f, kMaxKnob},           // Flow          :178
    {0.05f, kMaxKnob},           // Mallet        :180
    {1.0f, kMaxKnob},            // Bow Timbre    :177
    {1.0f, kMaxKnob},            // Blow Timbre   :179
    {1.0f, kMaxStrikeTimbre},    // Strike Timbre :181
    {0.05f, kMaxKnob},           // Geometry      :182
    {1.0f, kMaxKnob},            // Brightness    :183
    {1.0f, kMaxKnob},            // Damping       :184
    {0.01f, kMaxKnob},           // Position      :185
    {0.01f, 1.0f},               // Space         :186 (BIND allows 2; a pot alone reaches 1)
    {1.0f, 1.0f},                // Ext In: stepped, not smoothed
};
constexpr int kFirstSmoothed = 1;
constexpr int kLastSmoothed = 14;

// The per-block coefficient for a block of `count` native samples.
float blockCoefficient(float perBlock, int count) {
  if (count == ElementsEngine::kNativeBlock) return perBlock;
  return 1.0f - std::pow(1.0f - perBlock, float(count) / float(ElementsEngine::kNativeBlock));
}

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

float ElementsEngine::smoothedParam(int index) const {
  return index >= kFirstSmoothed && index <= kLastSmoothed ? smoothed_[index] : 0.0f;
}

void ElementsEngine::smooth(const ParamValues& p, int count) {
  if (snap_) {
    for (int i = kFirstSmoothed; i <= kLastSmoothed; ++i) {
      potLp_[i] = clamp01(p.p[i]);
      smoothed_[i] = std::min(potLp_[i], kControls[i].max);
    }
    snap_ = false;
    return;
  }
  const float potLp = blockCoefficient(kPotLp, count);
  for (int i = kFirstSmoothed; i <= kLastSmoothed; ++i) {
    potLp_[i] += potLp * (clamp01(p.p[i]) - potLp_[i]);
    const float value = std::min(potLp_[i], kControls[i].max);
    if (kControls[i].bindLp < 1.0f) {
      smoothed_[i] += blockCoefficient(kControls[i].bindLp, count) * (value - smoothed_[i]);
    } else {
      smoothed_[i] = value;
    }
  }
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
  snap_ = true;
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
  // Pot-only mapping of CvScaler::Read (CV term at 0) through the firmware's smoothing:
  // Contour and the three levels are the filtered linear pot; the rest are clamped by BIND
  // and, for Flow, Mallet, Geometry, Position and Space, filtered again (see kControls).
  smooth(p, count);
  elements::Patch* patch = s.part.mutable_patch();
  patch->exciter_envelope_shape = smoothed_[1];
  patch->exciter_bow_level = smoothed_[2];
  patch->exciter_blow_level = smoothed_[3];
  patch->exciter_strike_level = smoothed_[4];
  patch->exciter_blow_meta = smoothed_[5];
  patch->exciter_strike_meta = smoothed_[6];
  patch->exciter_bow_timbre = smoothed_[7];
  patch->exciter_blow_timbre = smoothed_[8];
  patch->exciter_strike_timbre = smoothed_[9];
  patch->exciter_signature = 0.0f;
  patch->resonator_geometry = smoothed_[10];
  patch->resonator_brightness = smoothed_[11];
  patch->resonator_damping = smoothed_[12];
  patch->resonator_position = smoothed_[13];
  patch->space = smoothed_[14];

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
