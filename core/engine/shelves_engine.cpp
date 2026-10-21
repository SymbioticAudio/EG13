#include "engine/shelves_engine.h"

#include <cstdio>

#include "engine/param_format.h"

namespace eg13 {

namespace {

// In ShelvesModel::Output's order.
const char* const kOutputNames[ShelvesEngine::kNumOutputs] = {
    "EQ", "Mid 1 LP", "Mid 1 BP", "Mid 1 HP", "Mid 2 LP", "Mid 2 BP", "Mid 2 HP"};

void formatGain(float v, char* buf, size_t size) { formatDb(ShelvesEngine::gainDb(v), buf, size); }
void formatQ(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "Q %.2f", double(ShelvesEngine::qFactor(v)));
}
void formatPad(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%s", ShelvesEngine::padOn(v) ? "-6 dB" : "Off");
}
void formatOutput(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%s", kOutputNames[ShelvesEngine::outputIndex(v)]);
}

// VCV's parameters in VCV's order (Shelves.cpp:17-26) and defaults: frequencies at the middle
// of their log range (Shelves.cpp:7,66-69), gains 0 dB (Shelves.cpp:71-74), Qs at the middle
// (Shelves.cpp:12,76-77), pre-gain off (Shelves.cpp:107). Output picks the main output or one
// of the six filter taps (Shelves.cpp:93-99).
const EngineParamInfo kParams[12] = {
    {"High Freq", 0.5f, formatKnobHz},  {"High Gain", 0.5f, formatGain}, {"Mid 1 Freq", 0.5f, formatKnobHz},
    {"Mid 1 Gain", 0.5f, formatGain},   {"Mid 1 Q", 0.5f, formatQ},      {"Mid 2 Freq", 0.5f, formatKnobHz},
    {"Mid 2 Gain", 0.5f, formatGain},   {"Mid 2 Q", 0.5f, formatQ},      {"Low Freq", 0.5f, formatKnobHz},
    {"Low Gain", 0.5f, formatGain},     {"Input Pad", 0.25f, formatPad}, {"Output", 0.5f / 7.0f, formatOutput},
};

const EngineInfo kInfo = {"shelves", "EQ", 12, kParams, true, false, 0, nullptr};

// The gain knobs' -1..1 (Shelves.cpp:130-133).
float gainKnob(float normalized) { return 2.0f * normalized - 1.0f; }

}  // namespace

ShelvesEngine::ShelvesEngine() = default;

ShelvesEngine::~ShelvesEngine() = default;

const EngineInfo& ShelvesEngine::staticInfo() { return kInfo; }

const EngineInfo& ShelvesEngine::info() const { return kInfo; }

float ShelvesEngine::gainDb(float normalized) { return gainKnob(clamp01(normalized)) * 18.0f; }

float ShelvesEngine::qFactor(float normalized) { return knobQ(normalized); }

int ShelvesEngine::outputIndex(float normalized) { return stepIndex(normalized, kNumOutputs); }

bool ShelvesEngine::padOn(float normalized) { return clamp01(normalized) >= 0.5f; }

float ShelvesEngine::smoothedParam(int index) const {
  return index >= 0 && index < kNumKnobs ? ramps_[index].value() : 0.0f;
}

void ShelvesEngine::prepare(double hostRate, int maxBlock) {
  rate_ = hostRate;
  rightOut_.assign(size_t(maxBlock), 0.0f);
  link_.prepare(hostRate);
  reset();
}

void ShelvesEngine::reset() {
  left_.reset(float(rate_));
  right_.copyFrom(left_);
  for (auto& ramp : ramps_) ramp.reset();
  link_.reset();
}

void ShelvesEngine::process(const float* const* in, float* const* out, const ParamValues& p,
                            int n) {
  // The VCV module reads its knobs every sample (Shelves.cpp:125-136); the model low-passes
  // frequency and Q like the hardware's CV amplifiers (shelves.hpp:370-380). EG13's parameters
  // change at block boundaries, so they ramp across each block instead of stepping. ParamRamp
  // takes clamped targets.
  for (int k = 0; k < kNumKnobs; ++k) ramps_[k].begin(clamp01(p.p[k]), n);
  const bool pad = padOn(p.p[10]);
  const int output = outputIndex(p.p[11]);
  if (link_.request(stereo_, true) == StereoLink::Start::CopyLeft) right_.copyFrom(left_);
  const bool right = link_.rightRuns();
  for (int i = 0; i < n; ++i) {
    const ShelvesModel::Knobs knobs = {
        ramps_[0].at(i), gainKnob(ramps_[1].at(i)), ramps_[2].at(i),
        gainKnob(ramps_[3].at(i)), ramps_[4].at(i), ramps_[5].at(i),
        gainKnob(ramps_[6].at(i)), ramps_[7].at(i), ramps_[8].at(i),
        gainKnob(ramps_[9].at(i))};
    out[0][i] = left_.process(knobs, pad, (in != nullptr ? in[0][i] : 0.0f) * kVolts, output) / kVolts;
    if (right) {
      rightOut_[size_t(i)] =
          right_.process(knobs, pad, (in != nullptr ? in[1][i] : 0.0f) * kVolts, output) / kVolts;
    }
  }
  link_.mix(out[0], rightOut_.data(), out[1], n);
}

}  // namespace eg13
