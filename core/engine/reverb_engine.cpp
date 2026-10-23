#include "engine/reverb_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "elements/dsp/fx/reverb.h"
#include "engine/param_format.h"
#include "engine/part_fx_engine.h"

namespace eg13 {

namespace {

void formatOffOn(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%s", partFxSwitch(v) ? "On" : "Off");
}

const EngineParamInfo kParams[9] = {
    {"Amount", kPartFxHostDefaults[0], formatPercent01},
    {"Time", kPartFxHostDefaults[1], formatPercent01},
    {"Diffusion", kPartFxHostDefaults[2], formatPercent01},
    {"Brightness", kPartFxHostDefaults[3], formatPercent01},
    {"(unused)", kPartFxHostDefaults[4], nullptr},
    {"(unused)", kPartFxHostDefaults[5], nullptr},
    {"(unused)", kPartFxHostDefaults[6], nullptr},
    {"(unused)", kPartFxHostDefaults[7], nullptr},
    {"Freeze", kPartFxHostDefaults[8], formatOffOn},  // switch: on at 50 % or more
};

const EngineInfo kInfo = {"elements_reverb", "Reverb", 9, kParams, true, false, 0, nullptr};

constexpr int kBufferSize = 32768;  // uint16_t words: FxEngine<32768, FORMAT_16_BIT>, 64 KiB
constexpr float kInputGain = 0.2f;  // elements/dsp/part.cc:247

// Elements' pot filter: CvScaler::Read runs once per 16-sample block and low-passes every pot
// with pot_lp_ += 0.01 * (raw - pot_lp_) (elements/cv_scaler.cc:149; Space, which drives this
// reverb on the module, is bound with 0.01 at :186). Freeze and the unused P's pass straight.
const float kPotLp[9] = {0.01f, 0.01f, 0.01f, 0.01f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};

}  // namespace

struct ReverbEngine::Impl {
  elements::Reverb reverb;
  std::vector<uint16_t> buffer = std::vector<uint16_t>(kBufferSize);
  float l[kNativeBlock];
  float r[kNativeBlock];
};

ReverbEngine::ReverbEngine() : impl_(std::make_unique<Impl>()), pots_(kPotLp, kInfo.paramCount, kNativeBlock) {
  reset();
}

ReverbEngine::~ReverbEngine() = default;

const EngineInfo& ReverbEngine::staticInfo() { return kInfo; }

const EngineInfo& ReverbEngine::info() const { return kInfo; }

float ReverbEngine::timeFor(float p) { return 0.35f + 0.63f * clamp01(p); }

float ReverbEngine::diffusionFor(float p) { return 0.55f + 0.15f * clamp01(p); }

float ReverbEngine::lpFor(float p) { return 0.6f + 0.37f * clamp01(p); }

float ReverbEngine::smoothedParam(int index) const {
  return index >= 0 && index < kInfo.paramCount ? pots_[index] : 0.0f;
}

void ReverbEngine::prepare(double, int) { reset(); }

// Audio-thread safe: memset + Init, no allocation. Reverb::Init sets only lp_, diffusion_ and
// the delay memory (elements/dsp/fx/reverb.h:43-49); Process reads amount_, input_gain_,
// reverb_time_ and the loop filters' state, so the memset makes a reset reverb a fresh one
// (preflight rulings G5, G7).
void ReverbEngine::reset() {
  std::memset(static_cast<void*>(&impl_->reverb), 0, sizeof(impl_->reverb));
  impl_->reverb.Init(impl_->buffer.data());
  pots_.reset();
}

void ReverbEngine::process(const float* const* in, float* const* out, const ParamValues& p, int n) {
  for (int done = 0; done < n;) {
    const int count = std::min(kNativeBlock, n - done);
    renderBlock(in, out, p, done, count);
    done += count;
  }
}

// One Elements block (elements/dsp/part.cc:237-249): the setters, then Process in place.
void ReverbEngine::renderBlock(const float* const* in, float* const* out, const ParamValues& p, int offset,
                               int count) {
  Impl& s = *impl_;
  for (int i = 0; i < count; ++i) {
    s.l[i] = in != nullptr ? in[0][offset + i] : 0.0f;
    s.r[i] = in != nullptr ? in[1][offset + i] : 0.0f;
  }
  pots_.update(p, count);
  s.reverb.set_amount(pots_[0]);
  s.reverb.set_diffusion(diffusionFor(pots_[2]));
  if (partFxSwitch(pots_[8])) {  // Freeze, as Elements freezes (part.cc:240-244)
    s.reverb.set_time(1.0f);
    s.reverb.set_input_gain(0.0f);
    s.reverb.set_lp(1.0f);
  } else {
    s.reverb.set_time(timeFor(pots_[1]));
    s.reverb.set_input_gain(kInputGain);
    s.reverb.set_lp(lpFor(pots_[3]));
  }
  s.reverb.Process(s.l, s.r, size_t(count));
  std::copy(s.l, s.l + count, out[0] + offset);
  std::copy(s.r, s.r + count, out[1] + offset);
}

}  // namespace eg13
