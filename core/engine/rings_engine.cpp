#include "engine/rings_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "rings/dsp/part.h"
#include "rings/dsp/patch.h"
#include "rings/dsp/performance_state.h"

namespace eg13 {

namespace {

const char* const kModelNames[RingsEngine::kNumModels] = {
    "Modal", "Sympathetic", "String", "FM Voice", "Sympathetic Q", "String + Reverb"};
const char* const kExciterNames[3] = {"Internal", "Input", "Both"};

void formatModel(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%s", kModelNames[RingsEngine::modelIndex(v)]);
}
void formatExciter(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%s", kExciterNames[RingsEngine::exciterIndex(v)]);
}
void formatPercent(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%d%%", int(std::lround(clamp01(v) * 100.0f)));
}
void formatSemitones(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%+.1f st", double(RingsEngine::frequencyOffset(v)));
}

const EngineParamInfo kParams[8] = {
    {"Model", 0.5f / 6.0f, formatModel},
    {"Structure", 0.5f, formatPercent},
    {"Brightness", 0.5f, formatPercent},
    {"Damping", 0.5f, formatPercent},
    {"Position", 0.5f, formatPercent},
    {"Exciter", 0.5f / 3.0f, formatExciter},  // stepped thirds; default Internal
    {"Frequency", 0.5f, formatSemitones},
    {"Spread", 1.0f, formatPercent},
};

const EngineInfo kInfo = {"rings", "Resonator", 8, kParams, true};

constexpr float kMaxKnob = 0.9995f;  // Rings' own clamp for structure, damping, position

}  // namespace

struct RingsEngine::Impl {
  rings::Part part;
  uint16_t reverb[32768];
  float in[kNativeBlock];
  float odd[kNativeBlock];
  float even[kNativeBlock];
};

RingsEngine::RingsEngine() : impl_(std::make_unique<Impl>()) { reset(); }

RingsEngine::~RingsEngine() = default;

const EngineInfo& RingsEngine::staticInfo() { return kInfo; }

const EngineInfo& RingsEngine::info() const { return kInfo; }

int RingsEngine::modelIndex(float normalized) {
  const int i = int(clamp01(normalized) * float(kNumModels));
  return i < kNumModels ? i : kNumModels - 1;
}

int RingsEngine::exciterIndex(float normalized) {
  const int i = int(clamp01(normalized) * 3.0f);
  return i < 3 ? i : 2;
}

float RingsEngine::frequencyOffset(float normalized) { return (clamp01(normalized) - 0.5f) * 24.0f; }

void RingsEngine::prepare(double hostRate, int) {
  pitchOffset_ = float(12.0 * std::log2(kNativeRate / hostRate));
  reset();
}

// Audio-thread safe by design: memset + Init, no allocation. Keep it that way.
void RingsEngine::reset() {
  // Part::Init() leaves several members (step counter, voice count) unset.
  std::memset(static_cast<void*>(&impl_->part), 0, sizeof(impl_->part));
  impl_->part.Init(impl_->reverb);
  impl_->part.set_polyphony(1);
  strum_ = false;
  model_ = -1;
}

void RingsEngine::noteOn(float note, float velocity) {
  note_ = note;
  velocity_ = clamp01(velocity);
  strum_ = true;
}

void RingsEngine::process(const float* const* in, float* const* out, const ParamValues& p, int n) {
  for (int done = 0; done < n;) {
    const int count = std::min(kNativeBlock, n - done);
    renderBlock(in, p, out, done, count);
    done += count;
  }
}

void RingsEngine::renderBlock(const float* const* in, const ParamValues& p, float* const* out,
                              int offset, int count) {
  Impl& s = *impl_;
  const int model = modelIndex(p.p[0]);
  if (model != model_) {
    s.part.set_model(rings::ResonatorModel(model));
    model_ = model;
  }
  rings::Patch patch;
  patch.structure = std::min(clamp01(p.p[1]), kMaxKnob);
  patch.brightness = clamp01(p.p[2]);
  patch.damping = std::min(clamp01(p.p[3]), kMaxKnob);
  patch.position = std::min(clamp01(p.p[4]), kMaxKnob);

  // Internal: the note-on strike only. Input: the input only. Both: strike plus
  // input. Upstream Rings always feeds `in` to the resonator, and
  // internal_exciter only adds the strike/pluck and selects the excitation
  // filter (note-tracked for the internal exciter, rings/dsp/part.cc:504-516).
  // So Both is internal_exciter with the input passed: the hardware state with
  // the input jack unpatched, here with a real signal on it.
  const int exciter = exciterIndex(p.p[5]);
  const bool useInput = exciter != 0 && in != nullptr;
  rings::PerformanceState state;
  state.strum = strum_;
  state.internal_exciter = exciter != 1 || in == nullptr;
  state.internal_strum = false;
  state.internal_note = true;
  state.tonic = note_ + frequencyOffset(p.p[6]) + pitchOffset_;
  state.note = 0.0f;
  state.fm = 0.0f;
  state.chord = std::clamp(int(std::lround(patch.structure * 10.0f)), 0, 10);
  strum_ = false;

  for (int i = 0; i < count; ++i) {
    s.in[i] = useInput ? 0.5f * (in[0][offset + i] + in[1][offset + i]) : 0.0f;
  }
  std::memset(s.odd, 0, sizeof(s.odd));
  std::memset(s.even, 0, sizeof(s.even));
  s.part.Process(state, patch, s.in, s.odd, s.even, size_t(count));

  const float spread = clamp01(p.p[7]);
  for (int i = 0; i < count; ++i) {
    out[0][offset + i] = (s.odd[i] + (1.0f - spread) * s.even[i]) * velocity_;
    out[1][offset + i] = (s.even[i] + (1.0f - spread) * s.odd[i]) * velocity_;
  }
}

}  // namespace eg13
