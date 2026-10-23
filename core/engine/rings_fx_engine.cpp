#include "engine/rings_fx_engine.h"

#include <algorithm>
#include <cstdio>

#include "engine/param_format.h"
#include "engine/part_fx_engine.h"

namespace eg13 {

namespace {

void formatModel(float v, char* buf, size_t size) {
  static const char* const names[RingsEngine::kNumModels] = {"Modal",    "Sympathetic",   "String",
                                                             "FM Voice", "Sympathetic Q", "String + Reverb"};
  std::snprintf(buf, size, "%s", names[partFxOption(v, RingsEngine::kNumModels)]);
}

void formatPolyphony(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%d", 1 << partFxOption(v, 3));  // 1, 2, 4 voices
}

void formatTune(float v, char* buf, size_t size) {
  formatSemitones(RingsEngine::frequencyOffset(v), buf, size);
}

const EngineParamInfo kParams[13] = {
    {"Mix", kPartFxHostDefaults[0], formatPercent01},
    {"Structure", kPartFxHostDefaults[1], formatPercent01},
    {"Brightness", kPartFxHostDefaults[2], formatPercent01},
    {"Damping", kPartFxHostDefaults[3], formatPercent01},
    {"Spread", kPartFxHostDefaults[4], formatPercent01},
    {"Position", kPartFxHostDefaults[5], formatPercent01},
    {"(unused)", kPartFxHostDefaults[6], nullptr},
    {"(unused)", kPartFxHostDefaults[7], nullptr},
    {"(unused)", kPartFxHostDefaults[8], nullptr},
    {"Model", kPartFxHostDefaults[9], formatModel},          // 6 options in 16 steps, append-only
    {"Polyphony", kPartFxHostDefaults[10], formatPolyphony},  // 1 / 2 / 4 in 16 steps, append-only
    {"(unused)", kPartFxHostDefaults[11], nullptr},
    {"Tune", kPartFxHostDefaults[12], formatTune},
};

const EngineInfo kInfo = {"rings_fx", "Resonator", 13, kParams, true, false, 0, nullptr};

// The normalized Source value at the middle of option `option` of `options` equal ranges.
float middleOf(int option, int options) { return (float(option) + 0.5f) / float(options); }

}  // namespace

RingsFxEngine::RingsFxEngine() : inner_(std::make_unique<RingsEngine>()) {}

RingsFxEngine::~RingsFxEngine() = default;

const EngineInfo& RingsFxEngine::staticInfo() { return kInfo; }

const EngineInfo& RingsFxEngine::info() const { return kInfo; }

ParamValues RingsFxEngine::sourceParams(const ParamValues& fx) {
  ParamValues s;
  s.p[0] = middleOf(partFxOption(fx.p[9], RingsEngine::kNumModels), RingsEngine::kNumModels);  // Model
  s.p[1] = fx.p[1];                                                                           // Structure
  s.p[2] = fx.p[2];                                                                           // Brightness
  s.p[3] = fx.p[3];                                                                           // Damping
  s.p[4] = fx.p[5];                                                                           // Position
  s.p[5] = 1.5f / 3.0f;                                                                       // Exciter: Input
  s.p[6] = fx.p[12];                                                                          // Frequency
  s.p[7] = fx.p[4];                                                                           // Spread
  s.p[8] = middleOf(partFxOption(fx.p[10], 3), 3);                                            // Polyphony
  return s;
}

void RingsFxEngine::prepare(double hostRate, int maxBlock) {
  inner_->prepare(hostRate, maxBlock);
  mix_.reset();
}

void RingsFxEngine::reset() {
  inner_->reset();
  mix_.reset();
}

void RingsFxEngine::noteOn(float note, float) { inner_->noteOn(note, 1.0f); }  // a strum at full level

void RingsFxEngine::setNote(float note) { inner_->setNote(note); }

// Whole native blocks go to the Source Resonator, then Mix: (1 - m) dry + m wet with m ramping
// linearly across the block, so Mix 100 % is the wet signal exactly.
void RingsFxEngine::process(const float* const* in, float* const* out, const ParamValues& p, int n) {
  const ParamValues source = sourceParams(p);
  const float mix = clamp01(p.p[0]);
  float wetL[kNativeBlock], wetR[kNativeBlock];
  for (int done = 0; done < n;) {
    const int count = std::min(kNativeBlock, n - done);
    const float* blockIn[2] = {in != nullptr ? in[0] + done : nullptr, in != nullptr ? in[1] + done : nullptr};
    float* wet[2] = {wetL, wetR};
    inner_->process(in != nullptr ? blockIn : nullptr, wet, source, count);
    mix_.begin(mix, count);
    for (int i = 0; i < count; ++i) {
      const float m = mix_.at(i);
      const float dryL = in != nullptr ? in[0][done + i] : 0.0f;
      const float dryR = in != nullptr ? in[1][done + i] : 0.0f;
      out[0][done + i] = (1.0f - m) * dryL + m * wetL[i];
      out[1][done + i] = (1.0f - m) * dryR + m * wetR[i];
    }
    done += count;
  }
}

}  // namespace eg13
