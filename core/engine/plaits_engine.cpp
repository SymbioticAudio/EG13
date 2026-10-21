#include "engine/plaits_engine.h"

#include <cmath>
#include <cstdio>

#include "plaits/dsp/voice.h"

namespace eg13 {

namespace {

constexpr size_t kRamSize = 16384;

const char* const kModelNames[PlaitsEngine::kNumModels] = {
    "Filtered waveforms", "Phase distortion", "6-op FM 1",        "6-op FM 2",
    "6-op FM 3",          "Wave terrain",     "String machine",   "Chiptune",
    "Virtual analog",     "Waveshaping",      "2-op FM",          "Granular formant",
    "Harmonic",           "Wavetable",        "Chords",           "Speech",
    "Granular cloud",     "Filtered noise",   "Particle noise",   "Inharmonic string",
    "Modal resonator",    "Bass drum",        "Snare drum",       "Hi-hat",
};

void formatModel(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%s", kModelNames[PlaitsEngine::modelIndex(v)]);
}

void formatPercent(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%d%%", int(std::lround(clamp01(v) * 100.0f)));
}

void formatSemitones(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%+.1f st", double(PlaitsEngine::frequencyOffset(v)));
}

const EngineParamInfo kParams[8] = {
    {"Model", (8.0f + 0.5f) / 24.0f, formatModel},
    {"Harmonics", 0.5f, formatPercent},
    {"Timbre", 0.5f, formatPercent},
    {"Morph", 0.5f, formatPercent},
    {"Frequency", 0.5f, formatSemitones},
    {"Decay", 0.5f, formatPercent},
    {"LPG Colour", 0.5f, formatPercent},
    {"Aux Mix", 0.0f, formatPercent},
};

const EngineInfo kInfo = {"plaits", "Macro Oscillator", 8, kParams, false};

// Firmware pot smoothing, per 12-sample block: Ui::Poll runs once per audio block
// (plaits.cc:81, dsp.h:51 kBlockSize = 12) and each PotController applies
// ONE_POLE(value_, pot, lp_coefficient) (pot_controller.h:106), coefficients from
// ui.cc:72-79. Decay and LPG Colour are the hidden parameters of the Morph and Timbre pots:
// ProcessUIRate copies the same filtered value_ to them (pot_controller.h:127-128), every
// fourth Poll (ui.cc:477-479); that 4-block hold is not modelled. Model is stepped. Aux Mix
// has no firmware control (OUT and AUX are separate jacks) and is not smoothed. The played
// note is not touched: it reaches Plaits as patch.note, outside the V/OCT path.
const float kPotLp[8] = {
    1.0f,    // Model: stepped
    0.005f,  // Harmonics  ui.cc:74
    0.01f,   // Timbre     ui.cc:76-77
    0.01f,   // Morph      ui.cc:78-79
    0.005f,  // Frequency  ui.cc:72-73 (transposition_)
    0.01f,   // Decay      Morph pot's hidden parameter
    0.01f,   // LPG Colour Timbre pot's hidden parameter
    1.0f,    // Aux Mix: EG13-only, de-clicked per sample instead (kMixSeconds)
};

// Aux Mix has no firmware filter; a ~5 ms per-sample one-pole keeps it from zippering.
constexpr double kMixSeconds = 0.005;

}  // namespace

static_assert(PlaitsEngine::kNativeBlock == int(plaits::kBlockSize), "Plaits native block");

struct PlaitsEngine::Impl {
  plaits::Voice voice;
  char ram[kRamSize];
  plaits::Voice::Frame frames[plaits::kBlockSize];
};

PlaitsEngine::PlaitsEngine()
    : impl_(std::make_unique<Impl>()),
      pots_(kPotLp, kInfo.paramCount, kNativeBlock),
      auxMix_(kMixSeconds) {
  reset();
}

PlaitsEngine::~PlaitsEngine() = default;

const EngineInfo& PlaitsEngine::staticInfo() { return kInfo; }

const EngineInfo& PlaitsEngine::info() const { return kInfo; }

int PlaitsEngine::modelIndex(float normalized) {
  const int index = int(clamp01(normalized) * float(kNumModels));
  return index < kNumModels ? index : kNumModels - 1;
}

float PlaitsEngine::frequencyOffset(float normalized) {
  return (clamp01(normalized) - 0.5f) * 24.0f;
}

float PlaitsEngine::smoothedParam(int index) const {
  return index >= 0 && index < kInfo.paramCount ? pots_[index] : 0.0f;
}

void PlaitsEngine::prepare(double hostRate, int) {
  pitchOffset_ = float(12.0 * std::log2(kNativeRate / hostRate));
  auxMix_.prepare(hostRate);
  reset();
}

void PlaitsEngine::reset() {
  stmlib::BufferAllocator allocator(impl_->ram, kRamSize);
  impl_->voice.Init(&allocator);
  gate_ = false;
  retrigger_ = false;
  pos_ = plaits::kBlockSize;
  pots_.reset();
  auxMix_.reset();
}

void PlaitsEngine::noteOn(float note, float velocity) {
  note_ = note;
  velocity_ = clamp01(velocity);
  gate_ = true;
  retrigger_ = true;
}

void PlaitsEngine::noteOff() { gate_ = false; }

void PlaitsEngine::renderBlock(const ParamValues& p) {
  pots_.update(p, kNativeBlock);
  plaits::Patch patch = {};
  patch.note = note_ + frequencyOffset(pots_[4]) + pitchOffset_;
  patch.harmonics = pots_[1];
  patch.timbre = pots_[2];
  patch.morph = pots_[3];
  patch.engine = modelIndex(p.p[0]);
  patch.decay = pots_[5];
  patch.lpg_colour = pots_[6];

  plaits::Modulations mods = {};
  mods.trigger_patched = true;
  mods.level_patched = true;
  // A low block before a new note gives Plaits a rising edge even when the
  // gate was already high (mono retrigger).
  mods.trigger = (gate_ && !retrigger_) ? 1.0f : 0.0f;
  mods.level = gate_ ? velocity_ : 0.0f;
  retrigger_ = false;

  impl_->voice.Render(patch, mods, impl_->frames, plaits::kBlockSize);
}

void PlaitsEngine::process(const float* const*, float* const* out, const ParamValues& p,
                           int n) {
  const float aux = clamp01(p.p[7]);
  for (int i = 0; i < n; ++i) {
    if (pos_ == plaits::kBlockSize) {
      renderBlock(p);
      pos_ = 0;
    }
    const plaits::Voice::Frame& f = impl_->frames[pos_++];
    const float mix = auxMix_.next(aux);
    const float s = ((1.0f - mix) * float(f.out) + mix * float(f.aux)) / 32768.0f;
    out[0][i] = s;
    out[1][i] = s;
  }
}

}  // namespace eg13
