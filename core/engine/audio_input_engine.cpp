#include "engine/audio_input_engine.h"

#include <cmath>
#include <cstdio>

namespace eg13 {

namespace {

void formatGain(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%+.1f dB", double(AudioInputEngine::gainDb(v)));
}
void formatTime(float seconds, char* buf, size_t size) {
  if (seconds < 1.0f) {
    std::snprintf(buf, size, "%d ms", int(std::lround(seconds * 1000.0f)));
  } else {
    std::snprintf(buf, size, "%.2f s", double(seconds));
  }
}
void formatAttack(float v, char* buf, size_t size) {
  formatTime(AudioInputEngine::attackSeconds(v), buf, size);
}
void formatRelease(float v, char* buf, size_t size) {
  formatTime(AudioInputEngine::releaseSeconds(v), buf, size);
}

const float kDefaultGain = 24.0f / 36.0f;                                // 0 dB
const float kDefaultAttack = float(std::log(5.0) / std::log(2000.0));    // 5 ms
const float kDefaultRelease = float(std::log(10.0) / std::log(1000.0));  // 50 ms

const EngineParamInfo kParams[3] = {
    {"Gain", kDefaultGain, formatGain},
    {"Attack", kDefaultAttack, formatAttack},
    {"Release", kDefaultRelease, formatRelease},
};

const EngineInfo kInfo = {"input", "Audio Input", 3, kParams, true};

// Gain multiplies the input directly, so a step at a control-rate boundary would be a step
// in the output (a click). A 1 ms one-pole on the linear gain spreads each step over a few
// control periods (16 host samples, ~0.33 ms at 48 kHz) while keeping fast modulation.
// Attack and Release need none: they only set the envelope's own one-pole rates.
constexpr double kGainSmoothingSeconds = 0.001;

}  // namespace

const EngineInfo& AudioInputEngine::staticInfo() { return kInfo; }

const EngineInfo& AudioInputEngine::info() const { return kInfo; }

float AudioInputEngine::gainDb(float normalized) { return -24.0f + 36.0f * clamp01(normalized); }

float AudioInputEngine::attackSeconds(float normalized) {
  return 0.001f * std::pow(2000.0f, clamp01(normalized));
}

float AudioInputEngine::releaseSeconds(float normalized) {
  return 0.005f * std::pow(1000.0f, clamp01(normalized));
}

void AudioInputEngine::prepare(double hostRate, int) {
  rate_ = hostRate;
  gainCoeff_ = float(1.0 - std::exp(-1.0 / (kGainSmoothingSeconds * hostRate)));
  reset();
}

void AudioInputEngine::reset() {
  gate_ = false;
  envelope_ = 0.0;
  snap_ = true;
}

void AudioInputEngine::noteOn(float, float velocity) {
  velocity_ = clamp01(velocity);  // also retargets the envelope when the gate is already on
  gate_ = true;
}

void AudioInputEngine::process(const float* const* in, float* const* out, const ParamValues& p,
                               int n) {
  const float gain = std::pow(10.0f, gainDb(p.p[0]) / 20.0f);
  if (snap_) {
    gain_ = gain;
    snap_ = false;
  }
  const double attack = 1.0 - std::exp(-1.0 / (double(attackSeconds(p.p[1])) * rate_));
  const double release = 1.0 - std::exp(-1.0 / (double(releaseSeconds(p.p[2])) * rate_));
  const double target = gate_ ? double(velocity_) : 0.0;
  for (int i = 0; i < n; ++i) {
    // Double state: a float one-pole stalls short of its target once the step drops below
    // half a float ulp, which would keep the 1e-6 snap from ever firing.
    envelope_ += (target - envelope_) * (target > envelope_ ? attack : release);
    if (std::fabs(target - envelope_) < 1e-6) envelope_ = target;
    // Snaps once within 1e-6 or once a step no longer moves the float (it would stall).
    const float smoothed = gain_ + gainCoeff_ * (gain - gain_);
    gain_ = smoothed == gain_ || std::fabs(gain - smoothed) < 1e-6f ? gain : smoothed;
    const float g = gain_ * float(envelope_);
    out[0][i] = in ? in[0][i] * g : 0.0f;
    out[1][i] = in ? in[1][i] * g : 0.0f;
  }
}

}  // namespace eg13
