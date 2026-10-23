#include "engine/sst_filter_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "engine/param_format.h"
#include "sst/filters++.h"

namespace eg13 {

namespace {

namespace sfpp = sst::filtersplusplus;

struct ClassicType {
  sfpp::FilterModel model;
  sfpp::ModelConfig config;
  std::string name;
  bool character;  // uses sst's one extra coefficient (filter_impl.h:55-82)
  bool bipolar;    // that coefficient is bipolar (filter_impl.h:84-105)
};

// Every sst-filters model and configuration in upstream order (Filter::availableModels(), each
// model's availableModelConfigurations sorted): 205 at the pinned commit. This order is EG13's
// permanent type index, frozen by tests/data/classic_filter_types.txt. Built on first use (the
// engine's constructor, main thread), which also builds sst's own configuration tables, so
// later type changes do not allocate.
const std::vector<ClassicType>& types() {
  static const std::vector<ClassicType> table = [] {
    std::vector<ClassicType> t;
    for (auto model : sfpp::Filter::availableModels()) {
      for (const auto& config : sfpp::Filter::availableModelConfigurations(model, true)) {
        t.push_back({model, config, sfpp::details::FilterPayload::displayName(model, config),
                     sfpp::Filter::coefficientsExtraCount(model, config) > 0,
                     sfpp::Filter::coefficientsExtraIsBipolar(model, config, 0)});
      }
    }
    return t;
  }();
  return table;
}

// Comb filters need an external delay line per SIMD lane (filter_impl.h:188-193).
constexpr size_t kDelayLine = size_t(sst::filters::utilities::MAX_FB_COMB) +
                              size_t(sst::filters::utilities::SincTable::FIRipol_N);

void formatType(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%s", SstFilterEngine::typeName(SstFilterEngine::typeIndex(v)));
}

// Cutoff: the Analog Filter's 20 Hz .. 20 kHz curve, default fully open. Resonance 0..1,
// default 0. Model (the filter type) 0 is Surge's classic 12 dB low-pass. Character: sst's extra coefficient
// (OB-Xd morph, K35 saturation, Fast SVF shelf/bell gain, comb mix), centred by default.
const EngineParamInfo kParams[4] = {
    {"Cutoff", 1.0f, formatKnobHz},
    {"Resonance", 0.0f, formatPercent01},
    {"Model", 0.5f / float(SstFilterEngine::kTypeSteps), formatType},
    {"Character", 0.5f, formatPercent01},
};

const EngineInfo kInfo = {"sst", "Classic Filters", 4, kParams, true, false, 0, nullptr};

}  // namespace

struct SstFilterEngine::Impl {
  sfpp::Filter filter;
  std::vector<float> delay;  // comb delay lines, kDelayLine per lane
};

SstFilterEngine::SstFilterEngine() : impl_(std::make_unique<Impl>()) {
  types();
  impl_->delay.assign(4 * kDelayLine, 0.0f);
  for (int lane = 0; lane < 4; ++lane) {
    impl_->filter.provideDelayLine(lane, impl_->delay.data() + size_t(lane) * kDelayLine);
  }
}

SstFilterEngine::~SstFilterEngine() = default;

const EngineInfo& SstFilterEngine::staticInfo() { return kInfo; }

const EngineInfo& SstFilterEngine::info() const { return kInfo; }

int SstFilterEngine::typeCount() { return int(types().size()); }

int SstFilterEngine::typeIndex(float normalized) {
  const int i = int(clamp01(normalized) * float(kTypeSteps));
  return std::min(i, typeCount() - 1);
}

float SstFilterEngine::typeValue(int index) { return (float(index) + 0.5f) / float(kTypeSteps); }

const char* SstFilterEngine::typeName(int index) {
  return types()[size_t(std::clamp(index, 0, typeCount() - 1))].name.c_str();
}

bool SstFilterEngine::typeHasCharacter(int index) {
  return types()[size_t(std::clamp(index, 0, typeCount() - 1))].character;
}

float SstFilterEngine::cutoffNote(float normalized) {
  return 12.0f * std::log2(knobHz(normalized) / 440.0f);
}

void SstFilterEngine::prepare(double hostRate, int) {
  rate_ = hostRate;
  block_ = 16;
  impl_->filter.setSampleRateAndBlockSize(hostRate, size_t(block_));
  type_ = -1;  // selected again (prepareInstance) on the next process()
  reset();
}

void SstFilterEngine::reset() {
  impl_->filter.reset();
  std::fill(impl_->delay.begin(), impl_->delay.end(), 0.0f);
  snap_ = true;
}

void SstFilterEngine::selectType(int type) {
  const ClassicType& t = types()[size_t(type)];
  sfpp::Filter& f = impl_->filter;
  f.setFilterModel(t.model);
  f.setModelConfiguration(t.config);
  (void)f.prepareInstance();  // every listed type prepares (test_third_party_smoke.cpp); it resets
  f.setStereo();              // lanes 0 (L) and 1 (R) active
  type_ = type;
  snap_ = true;
}

void SstFilterEngine::process(const float* const* in, float* const* out, const ParamValues& p,
                              int n) {
  sfpp::Filter& f = impl_->filter;
  const int type = typeIndex(p.p[2]);
  if (type != type_) selectType(type);  // like the hardware's type switch: immediate
  if (n != block_) {
    f.setSampleRateAndBlockSize(rate_, size_t(n));
    block_ = n;
  }
  const ClassicType& t = types()[size_t(type_)];
  const float cutoff = cutoffNote(p.p[0]);
  const float reso = clamp01(p.p[1]);
  const float extra = t.bipolar ? 2.0f * clamp01(p.p[3]) - 1.0f : clamp01(p.p[3]);
  // sst-filters interpolates its coefficients across each block, as in Surge (dC = (tC - C) /
  // blockSize, FilterCoefficientMaker_Impl.h:848): that is this engine's smoothing. The first
  // block after a reset or a type change starts on its target.
  if (snap_) {
    f.makeConstantCoefficients(0, cutoff, reso, extra);
    snap_ = false;
  } else {
    f.makeCoefficients(0, cutoff, reso, extra);
  }
  f.copyCoefficientsFromVoiceToVoice(0, 1);
  f.prepareBlock();
  for (int i = 0; i < n; ++i) {
    const float l = in != nullptr ? in[0][i] : 0.0f;
    const float r = in != nullptr ? in[1][i] : 0.0f;
    f.processStereoSample(l, r, out[0][i], out[1][i]);
  }
  f.concludeBlock();
}

}  // namespace eg13
