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
void formatPolyphony(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%d", RingsEngine::polyphonyVoices(v));
}
void formatPercent(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%d%%", int(std::lround(clamp01(v) * 100.0f)));
}
void formatSemitones(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%+.1f st", double(RingsEngine::frequencyOffset(v)));
}

const EngineParamInfo kParams[9] = {
    {"Model", 0.5f / 6.0f, formatModel},
    {"Structure", 0.5f, formatPercent},
    {"Brightness", 0.5f, formatPercent},
    {"Damping", 0.5f, formatPercent},
    {"Position", 0.5f, formatPercent},
    {"Exciter", 0.5f / 3.0f, formatExciter},  // stepped thirds; default Internal
    {"Frequency", 0.5f, formatSemitones},
    {"Spread", 1.0f, formatPercent},
    {"Polyphony", 0.5f / 3.0f, formatPolyphony},  // stepped thirds; default 1 voice
};

const EngineInfo kInfo = {"rings", "Resonator", 9, kParams, true};

constexpr float kMaxKnob = 0.9995f;  // Rings' own clamp for structure, damping, position
constexpr float kTonic = 60.0f;      // reference the played note is measured from

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

int RingsEngine::polyphonyVoices(float normalized) {
  const int i = int(clamp01(normalized) * 3.0f);
  return 1 << (i < 3 ? i : 2);  // the firmware's button cycles 1, 2, 4 (rings/ui.cc:282-291)
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
  polyphony_ = 1;
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
  // Like the firmware's polyphony button (rings/ui.cc:282-291): set_polyphony
  // only marks the resonators dirty; Process re-inits them in place on the next
  // block (part.cc ConfigureResonators), with no allocation. Calling it only on
  // a change keeps ringing voices from being re-initialised every block.
  const int polyphony = polyphonyVoices(p.p[8]);
  if (polyphony != polyphony_) {
    s.part.set_polyphony(polyphony);
    polyphony_ = polyphony;
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
  // The played note goes in as the V/OCT note against a fixed C4 tonic, as the
  // firmware does with V/OCT patched (rings/cv_scaler.cc:193-197; Frequency is
  // the transpose knob). Part keeps a note per voice from it (part.cc:480-494),
  // so with polyphony above 1 earlier strums keep their own pitch; the tonic is
  // shared by every voice.
  state.internal_note = false;
  state.tonic = kTonic + frequencyOffset(p.p[6]) + pitchOffset_;
  state.note = note_ - kTonic;
  state.fm = 0.0f;
  state.chord = std::clamp(int(std::lround(patch.structure * 10.0f)), 0, 10);
  strum_ = false;

  for (int i = 0; i < count; ++i) {
    s.in[i] = useInput ? 0.5f * (in[0][offset + i] + in[1][offset + i]) : 0.0f;
  }
  std::memset(s.odd, 0, sizeof(s.odd));
  std::memset(s.even, 0, sizeof(s.even));
  s.part.Process(state, patch, s.in, s.odd, s.even, size_t(count));

  // At polyphony 1 out/aux are one voice's odd/even pickups (rings/dsp/part.cc:
  // 530-535) and the fold is out - aux, the firmware's own per-voice fold
  // (part.cc:540). Above 1 each voice is folded already and whole voices are
  // dispatched to out (0, 2) and aux (1, 3) with the same sign (part.cc:536-542),
  // so the mono sum is out + aux. String +
  // Reverb negates aux after the reverb at any polyphony (part.cc:559-561).
  const float evenSign =
      polyphony_ == 1 || model_ == rings::RESONATOR_MODEL_STRING_AND_REVERB ? -1.0f : 1.0f;
  const float spread = clamp01(p.p[7]);
  for (int i = 0; i < count; ++i) {
    const float odd = s.odd[i];
    const float even = evenSign * s.even[i];
    out[0][offset + i] = (odd + (1.0f - spread) * even) * velocity_;
    out[1][offset + i] = (even + (1.0f - spread) * odd) * velocity_;
  }
}

}  // namespace eg13
