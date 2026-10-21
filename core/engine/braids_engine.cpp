#include "engine/braids_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "braids/macro_oscillator.h"
#include "braids/signature_waveshaper.h"
#include "stmlib/utils/dsp.h"

namespace eg13 {

namespace {

const char* const kShapeNames[BraidsEngine::kNumShapes] = {
    "CSAW",          "Morph",          "Saw/Square",     "Fold",           "Buzz",
    "Square Sub",    "Saw Sub",        "Square Sync",    "Saw Sync",       "Triple Saw",
    "Triple Square", "Triple Triangle", "Triple Sine",   "Triple Ring",    "Saw Swarm",
    "Saw Comb",      "Toy",            "Filter LP",      "Filter Peak",    "Filter BP",
    "Filter HP",     "VOSIM",          "Vowel",          "Vowel FOF",      "Harmonics",
    "FM",            "Feedback FM",    "Chaotic FM",     "Plucked",        "Bowed",
    "Blown",         "Fluted",         "Bell",           "Drum",           "Kick",
    "Cymbal",        "Snare",          "Wavetables",     "Wave Map",       "Wave Line",
    "Wave Paraphonic", "Filtered Noise", "Twin Peaks",   "Clocked Noise",  "Granular Cloud",
    "Particle Noise", "Digital Mod",
};

// Braids firmware (braids.cc): RESOLUTION_2..16_BIT and SAMPLE_RATE_4K..96K.
const uint16_t kBitMasks[7] = {0xc000, 0xe000, 0xf000, 0xf800, 0xff00, 0xfff0, 0xffff};
const int kBitDepths[7] = {2, 3, 4, 6, 8, 12, 16};
const int kDecimation[7] = {24, 12, 6, 4, 3, 2, 1};
const int kRatesKHz[7] = {4, 8, 16, 24, 32, 48, 96};

int stepIndex(float normalized, int steps) {
  const int i = int(clamp01(normalized) * float(steps));
  return i < steps ? i : steps - 1;
}

void formatShape(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%s", kShapeNames[BraidsEngine::shapeIndex(v)]);
}
void formatPercent(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%d%%", int(std::lround(clamp01(v) * 100.0f)));
}
void formatSemitones(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%+.1f st", double(BraidsEngine::frequencyOffset(v)));
}
void formatBits(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%d bit", kBitDepths[BraidsEngine::bitDepthIndex(v)]);
}
void formatRate(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%d kHz", kRatesKHz[BraidsEngine::sampleRateIndex(v)]);
}

const EngineParamInfo kParams[7] = {
    {"Shape", 0.5f / 47.0f, formatShape},
    {"Timbre", 0.5f, formatPercent},
    {"Color", 0.5f, formatPercent},
    {"Frequency", 0.5f, formatSemitones},
    {"Signature", 0.0f, formatPercent},
    {"Bit Depth", 6.5f / 7.0f, formatBits},
    {"Sample Rate", 6.5f / 7.0f, formatRate},
};

const EngineInfo kInfo = {"braids", "Digital Oscillator", 7, kParams, false};

}  // namespace

struct BraidsEngine::Impl {
  braids::MacroOscillator osc;
  braids::SignatureWaveshaper waveshaper;
  uint8_t sync[kNativeBlock];
  int16_t buffer[kNativeBlock];
  int16_t held;
};

BraidsEngine::BraidsEngine() : impl_(std::make_unique<Impl>()) { reset(); }

BraidsEngine::~BraidsEngine() = default;

const EngineInfo& BraidsEngine::staticInfo() { return kInfo; }

const EngineInfo& BraidsEngine::info() const { return kInfo; }

int BraidsEngine::shapeIndex(float normalized) { return stepIndex(normalized, kNumShapes); }
int BraidsEngine::bitDepthIndex(float normalized) { return stepIndex(normalized, 7); }
int BraidsEngine::sampleRateIndex(float normalized) { return stepIndex(normalized, 7); }
float BraidsEngine::frequencyOffset(float normalized) { return (clamp01(normalized) - 0.5f) * 24.0f; }

void BraidsEngine::prepare(double hostRate, int) {
  pitchOffset_ = float(12.0 * std::log2(kNativeRate / hostRate));
  reset();
}

void BraidsEngine::reset() {
  // MacroOscillator::Init() leaves shape, pitch and parameters unset.
  std::memset(static_cast<void*>(&impl_->osc), 0, sizeof(impl_->osc));
  impl_->osc.Init();
  impl_->waveshaper.Init(0x0000);
  std::memset(impl_->sync, 0, sizeof(impl_->sync));
  impl_->held = 0;
  gate_ = false;
  strike_ = false;
  gain_ = 0.0f;
  phase_ = 0;
}

void BraidsEngine::noteOn(float note, float velocity) {
  note_ = note;
  velocity_ = clamp01(velocity);
  gate_ = true;
  strike_ = true;
}

void BraidsEngine::noteOff() { gate_ = false; }

void BraidsEngine::process(const float* const*, float* const* out, const ParamValues& p, int n) {
  for (int done = 0; done < n;) {
    const int count = std::min(kNativeBlock, n - done);
    renderBlock(p, out, done, count);
    done += count;
  }
}

void BraidsEngine::renderBlock(const ParamValues& p, float* const* out, int offset, int count) {
  Impl& s = *impl_;
  s.osc.set_shape(braids::MacroOscillatorShape(shapeIndex(p.p[0])));
  s.osc.set_parameters(int16_t(clamp01(p.p[1]) * 32767.0f), int16_t(clamp01(p.p[2]) * 32767.0f));
  const int pitch = int(std::lround((note_ + frequencyOffset(p.p[3]) + pitchOffset_) * 128.0f));
  s.osc.set_pitch(int16_t(std::clamp(pitch, 0, 16383)));
  if (strike_) {
    s.osc.Strike();
    strike_ = false;
  }
  s.osc.Render(s.sync, s.buffer, size_t(count));

  const uint16_t mask = kBitMasks[bitDepthIndex(p.p[5])];
  const int decimation = kDecimation[sampleRateIndex(p.p[6])];
  const float sig = clamp01(p.p[4]) * 4.0f;
  const uint16_t signature = uint16_t(std::min(65535.0f, sig * sig * 4095.0f));
  const float target = gate_ ? velocity_ : 0.0f;
  // Firmware order (braids/braids.cc:287-291): VCA, then the signature
  // waveshaper, so the level sets how hard it is driven. The waveshaper maps 0
  // to a non-zero level; subtract it so a closed VCA is silent and the voice
  // can be freed.
  const int16_t rest = stmlib::Mix(
      int16_t(0),
      int16_t(std::clamp(s.waveshaper.Transform(0), int32_t(-32768), int32_t(32767))), signature);
  for (int i = 0; i < count; ++i) {
    // phase_ persists across calls, so short calls keep a regular hold period
    // (every decimation factor divides kNativeBlock, as in the firmware).
    if (phase_ % decimation == 0) s.held = int16_t(s.buffer[i] & mask);
    phase_ = (phase_ + 1) % kNativeBlock;
    const int16_t sample = int16_t(float(s.held) * gain_);
    gain_ += (target - gain_) * (1.0f / 16.0f);
    if (gain_ < 1e-7f && target == 0.0f) gain_ = 0.0f;
    const int16_t warped =
        int16_t(std::clamp(s.waveshaper.Transform(sample), int32_t(-32768), int32_t(32767)));
    const int16_t mixed = stmlib::Mix(sample, warped, signature);
    const float v = float(int32_t(mixed) - int32_t(rest)) / 32768.0f;
    out[0][offset + i] = v;
    out[1][offset + i] = v;
  }
}

}  // namespace eg13
