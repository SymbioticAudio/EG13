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

}  // namespace

struct PlaitsEngine::Impl {
  plaits::Voice voice;
  char ram[kRamSize];
  plaits::Voice::Frame frames[plaits::kBlockSize];
};

PlaitsEngine::PlaitsEngine() : impl_(std::make_unique<Impl>()) { reset(); }

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

void PlaitsEngine::prepare(double hostRate, int) {
  pitchOffset_ = float(12.0 * std::log2(kNativeRate / hostRate));
  reset();
}

void PlaitsEngine::reset() {
  stmlib::BufferAllocator allocator(impl_->ram, kRamSize);
  impl_->voice.Init(&allocator);
  gate_ = false;
  retrigger_ = false;
  pos_ = plaits::kBlockSize;
}

void PlaitsEngine::noteOn(float note, float velocity) {
  note_ = note;
  velocity_ = clamp01(velocity);
  gate_ = true;
  retrigger_ = true;
}

void PlaitsEngine::noteOff() { gate_ = false; }

void PlaitsEngine::renderBlock(const ParamValues& p) {
  plaits::Patch patch = {};
  patch.note = note_ + frequencyOffset(p.p[4]) + pitchOffset_;
  patch.harmonics = clamp01(p.p[1]);
  patch.timbre = clamp01(p.p[2]);
  patch.morph = clamp01(p.p[3]);
  patch.engine = modelIndex(p.p[0]);
  patch.decay = clamp01(p.p[5]);
  patch.lpg_colour = clamp01(p.p[6]);

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
    const float s = ((1.0f - aux) * float(f.out) + aux * float(f.aux)) / 32768.0f;
    out[0][i] = s;
    out[1][i] = s;
  }
}

}  // namespace eg13
