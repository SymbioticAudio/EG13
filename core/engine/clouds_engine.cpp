#include "engine/clouds_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "clouds/dsp/granular_processor.h"
#include "engine/param_format.h"
#include "engine/part_fx_engine.h"

namespace eg13 {

namespace {

const char* const kModeNames[CloudsEngine::kNumModes] = {"Granular", "Stretch", "Looping delay", "Spectral"};
const char* const kQualityNames[CloudsEngine::kNumQualities] = {"16-bit stereo", "16-bit mono", "8-bit stereo",
                                                                "8-bit mono"};

void formatMode(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%s", kModeNames[CloudsEngine::modeIndex(v)]);
}

void formatQuality(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%s", kQualityNames[CloudsEngine::qualityIndex(v)]);
}

void formatOffOn(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%s", partFxSwitch(v) ? "On" : "Off");
}

void formatPitch(float v, char* buf, size_t size) { formatSemitones(CloudsEngine::pitchSemitones(v), buf, size); }

// M5 plan ruling 8: the P layout; every default equals the host default of its position.
const EngineParamInfo kParams[13] = {
    {"Blend", kPartFxHostDefaults[0], formatPercent01},
    {"Position", kPartFxHostDefaults[1], formatPercent01},
    {"Size", kPartFxHostDefaults[2], formatPercent01},
    {"Pitch", kPartFxHostDefaults[3], formatPitch},
    {"Density", kPartFxHostDefaults[4], formatPercent01},  // 75 %: Clouds makes no grains near 50 % (ruling 9)
    {"Texture", kPartFxHostDefaults[5], formatPercent01},
    {"Feedback", kPartFxHostDefaults[6], formatPercent01},
    {"Reverb", kPartFxHostDefaults[7], formatPercent01},
    {"Freeze", kPartFxHostDefaults[8], formatOffOn},     // switch: on at 50 % or more
    {"Mode", kPartFxHostDefaults[9], formatMode},        // stepped, append-only: 16 fixed steps
    {"Quality", kPartFxHostDefaults[10], formatQuality},  // stepped, append-only: 16 fixed steps
    {"Spread", kPartFxHostDefaults[11], formatPercent01},
    {"In Gain", kPartFxHostDefaults[12], formatPercent01},
};

const EngineInfo kInfo = {"clouds", "Granular", 13, kParams, true, false, 0, nullptr};

// The module's two memories, as the VCV port sizes them (Clouds.cpp:99-100).
constexpr size_t kLargeBufferSize = 118784;
constexpr size_t kSmallBufferSize = 65536 - 128;
constexpr float kFadeStep = 1.0f / float(CloudsEngine::kChangeFadeSamples);

// VCV's clamp to int16 (Clouds.cpp:151-152), then the conversion to short. NaN reads as silence.
inline short toInt16(float v) {
  if (!(v == v)) return 0;
  return short(std::min(std::max(v, -32768.0f), 32767.0f));
}

}  // namespace

struct CloudsEngine::Impl {
  std::vector<uint8_t> large = std::vector<uint8_t>(kLargeBufferSize);
  std::vector<uint8_t> small = std::vector<uint8_t>(kSmallBufferSize);
  clouds::GranularProcessor processor;
  clouds::ShortFrame input[kNativeBlock];
  clouds::ShortFrame output[kNativeBlock];
  bool triggerPending = false;
  // The Mode and Quality Clouds runs (-1: none yet; the first block applies the P's at once).
  int mode = -1;
  int quality = -1;
  float fade = 1.0f;  // the change fade's gain
};

CloudsEngine::CloudsEngine() : impl_(std::make_unique<Impl>()) { reset(); }

CloudsEngine::~CloudsEngine() = default;

const EngineInfo& CloudsEngine::staticInfo() { return kInfo; }

const EngineInfo& CloudsEngine::info() const { return kInfo; }

float CloudsEngine::pitchSemitones(float p) { return (2.0f * clamp01(p) - 1.0f) * 24.0f; }

float CloudsEngine::inputGain(float p) { return 2.0f * clamp01(p); }

int CloudsEngine::modeIndex(float p) { return partFxOption(p, kNumModes); }

int CloudsEngine::qualityIndex(float p) { return partFxOption(p, kNumQualities); }

void CloudsEngine::prepare(double, int) { reset(); }

// Audio-thread safe: memset + Init, no allocation (preflight rulings G6, G7). Init leaves members
// unset (as the VCV port memsets, Clouds.cpp:103-104); the memories are cleared too, so a reset
// engine equals a fresh one whichever Mode and Quality it ran.
void CloudsEngine::reset() {
  Impl& s = *impl_;
  std::fill(s.large.begin(), s.large.end(), uint8_t(0));
  std::fill(s.small.begin(), s.small.end(), uint8_t(0));
  std::memset(static_cast<void*>(&s.processor), 0, sizeof(s.processor));
  s.processor.Init(s.large.data(), s.large.size(), s.small.data(), s.small.size());
  s.triggerPending = false;
  s.mode = -1;
  s.quality = -1;
  s.fade = 1.0f;
}

// A trigger for the next native block (Clouds.cpp:133-135,162-163, cleared at :214).
void CloudsEngine::noteOn(float, float) { impl_->triggerPending = true; }

void CloudsEngine::process(const float* const* in, float* const* out, const ParamValues& p, int n) {
  // Native blocks; the slot's wrapper always gives whole ones (preflight ruling G8). A shorter
  // call runs a shorter Clouds block (see renderBlock).
  for (int done = 0; done < n;) {
    const int count = std::min(kNativeBlock, n - done);
    renderBlock(in, out, p, done, count);
    done += count;
  }
}

// One block of the VCV port's loop (Clouds.cpp:139-197) with EG13's P's.
void CloudsEngine::renderBlock(const float* const* in, float* const* out, const ParamValues& p, int offset,
                               int count) {
  Impl& s = *impl_;
  const float gain = inputGain(p.p[12]);
  for (int i = 0; i < count; ++i) {
    const float l = in != nullptr ? in[0][offset + i] : 0.0f;
    const float r = in != nullptr ? in[1][offset + i] : 0.0f;
    s.input[i].l = toInt16(l * gain * 32767.0f);
    s.input[i].r = toInt16(r * gain * 32767.0f);
  }
  // Clouds' 8-bit qualities halve the rate with a 2:1 converter that needs an even block size
  // (clouds/dsp/sample_rate_converter.h:52-63; an odd one never ends). Outside the whole-block
  // contract, an odd block is padded with its last sample and the extra output is dropped.
  int size = count;
  if (size % 2 != 0) {
    s.input[size] = s.input[size - 1];
    ++size;
  }

  // Mode and Quality (G10): the first block after reset applies them at once; later a change
  // fades the output out, applies, and fades back in.
  const int mode = modeIndex(p.p[9]);
  const int quality = qualityIndex(p.p[10]);
  const bool changed = mode != s.mode || quality != s.quality;
  if (s.mode < 0 || (changed && s.fade <= 0.0f)) {
    s.mode = mode;
    s.quality = quality;
  }
  const bool fadingOut = mode != s.mode || quality != s.quality;

  s.processor.set_playback_mode(clouds::PlaybackMode(s.mode));
  s.processor.set_quality(s.quality);
  s.processor.Prepare();

  clouds::Parameters* q = s.processor.mutable_parameters();
  q->trigger = s.triggerPending;
  q->gate = s.triggerPending;
  q->freeze = partFxSwitch(p.p[8]);
  q->position = clamp01(p.p[1]);
  q->size = clamp01(p.p[2]);
  q->pitch = pitchSemitones(p.p[3]);
  q->density = clamp01(p.p[4]);
  q->texture = clamp01(p.p[5]);
  q->dry_wet = clamp01(p.p[0]);
  q->stereo_spread = clamp01(p.p[11]);
  q->feedback = clamp01(p.p[6]);
  q->reverb = clamp01(p.p[7]);
  s.processor.Process(s.input, s.output, size_t(size));
  s.triggerPending = false;

  for (int i = 0; i < count; ++i) {
    out[0][offset + i] = float(s.output[i].l) / 32768.0f;
    out[1][offset + i] = float(s.output[i].r) / 32768.0f;
  }
  if (fadingOut || s.fade < 1.0f) {
    const float step = fadingOut ? -kFadeStep : kFadeStep;
    for (int i = 0; i < count; ++i) {
      s.fade = std::min(std::max(s.fade + step, 0.0f), 1.0f);
      out[0][offset + i] *= s.fade;
      out[1][offset + i] *= s.fade;
    }
  }
}

}  // namespace eg13
