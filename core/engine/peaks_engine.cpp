#include "engine/peaks_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "peaks/gate_processor.h"
#include "peaks/modulations/lfo.h"
#include "peaks/modulations/multistage_envelope.h"
#include "peaks/resources.h"

namespace eg13 {

static_assert(int(kGateHigh) == int(peaks::GATE_FLAG_HIGH) && int(kGateRising) == int(peaks::GATE_FLAG_RISING) &&
                  int(kGateFalling) == int(peaks::GATE_FLAG_FALLING),
              "EG13's gate flags are Peaks'");

namespace {

// Append-only (kModSelectorSteps): never reorder, only add at the end.
const char* const kModeNames[PeaksEngine::kNumModes] = {"Envelope", "LFO", "Tap LFO", "Random"};
const char* const kWaveformNames[PeaksEngine::kNumWaveforms] = {"Sine", "Triangle", "Square", "Steps", "Noise"};

void formatMode(float v, char* buf, size_t size) { formatOption(kModeNames, PeaksEngine::kNumModes, v, buf, size); }
void formatWaveform(float v, char* buf, size_t size) {
  formatOption(kWaveformNames, PeaksEngine::kNumWaveforms, v, buf, size);
}
void formatEnvelopeTime(float v, char* buf, size_t size) {
  // MultistageEnvelope::Process: lut_env_increments[time >> 8] per sample over a 2^32 phase.
  const double seconds = 4294967296.0 / double(peaks::lut_env_increments[PeaksEngine::knob(v) >> 8]) / 48000.0;
  if (seconds < 1.0) {
    std::snprintf(buf, size, "%d ms", int(std::lround(seconds * 1000.0)));
  } else {
    std::snprintf(buf, size, "%.2f s", seconds);
  }
}
void formatRate(float v, char* buf, size_t size) {
  // Lfo::Process: lut_lfo_increments[rate >> 8] per sample over a 2^32 phase.
  const double hz = double(peaks::lut_lfo_increments[PeaksEngine::knob(v) >> 8]) * 48000.0 / 4294967296.0;
  std::snprintf(buf, size, hz < 10.0 ? "%.2f Hz" : "%.1f Hz", hz);
}

// Defaults are the mod slots' shared host defaults (param_table.cpp, kModHostDefaults): an ADSR
// with the fastest attack, about 0.4 s decay and release and full sustain; the LFOs at about
// 2.2 Hz, sine, neutral shape and phase.
const EngineParamInfo kParams[11] = {
    {"Mode", 0.0f, formatMode},
    {"Attack", 0.0f, formatEnvelopeTime},
    {"Rate", 0.5f, formatRate},
    {"Decay", 0.5f, formatEnvelopeTime},
    {"Wave Shape", 0.5f, formatPercent01},
    {"Sustain", 1.0f, formatPercent01},
    {"Phase", 0.5f, formatPercent01},
    {"Release", 0.5f, formatEnvelopeTime},
    {"Smoothness", 0.5f, formatPercent01},
    {"(unused)", 0.0f, nullptr},
    {"Waveform", 0.0f, formatWaveform},
};

const ModOutputInfo kOutputs[1] = {{"Out", true}};

const ModEngineInfo kInfo = {{"peaks", "Function Generator", 11, kParams, false}, 1, kOutputs, false};

}  // namespace

struct PeaksEngine::Impl {
  peaks::MultistageEnvelope envelope;
  peaks::Lfo lfo;
  int16_t out[kNativeBlock];
};

PeaksEngine::PeaksEngine() : NativeModEngine(kNativeRate, kNativeBlock), impl_(std::make_unique<Impl>()) {
  resetEngine();
}

PeaksEngine::~PeaksEngine() = default;

const ModEngineInfo& PeaksEngine::staticInfo() { return kInfo; }

const ModEngineInfo& PeaksEngine::info() const { return kInfo; }

int PeaksEngine::modeIndex(float normalized) { return modOption(normalized, kNumModes); }

int PeaksEngine::waveformIndex(float normalized) { return modOption(normalized, kNumWaveforms); }

uint16_t PeaksEngine::knob(float normalized) { return uint16_t(std::lround(clamp01(normalized) * 65535.0f)); }

uint16_t PeaksEngine::waveformValue(int waveform) {
  // Lfo::set_shape_integer: shape = value * LFO_SHAPE_LAST >> 16. The centre of each fifth.
  return uint16_t((uint32_t(waveform) * 65536u + 32768u) / uint32_t(kNumWaveforms));
}

void PeaksEngine::knobs(int mode, const ParamValues& p, uint16_t out[4]) {
  switch (mode) {
    case Envelope:  // set_adsr(attack, decay, sustain >> 1, release)
      out[0] = knob(p.p[1]);
      out[1] = knob(p.p[3]);
      out[2] = knob(p.p[5]);
      out[3] = knob(p.p[7]);
      return;
    case TapLfo:  // synced: level, shape, parameter, phase
      out[0] = kLfoLevel;
      out[1] = waveformValue(waveformIndex(p.p[10]));
      out[2] = knob(p.p[4]);
      out[3] = knob(p.p[6]);
      return;
    case Random:  // the noise shape: rate, shape, smoothness, phase 0
      out[0] = knob(p.p[2]);
      out[1] = waveformValue(peaks::LFO_SHAPE_NOISE);
      out[2] = knob(p.p[8]);
      out[3] = 32768;
      return;
    default:  // free LFO: rate, shape, parameter, phase
      out[0] = knob(p.p[2]);
      out[1] = waveformValue(waveformIndex(p.p[10]));
      out[2] = knob(p.p[4]);
      out[3] = knob(p.p[6]);
      return;
  }
}

// As Processors::Init leaves a channel (peaks/processors.cc:60-82): both processors initialised;
// the first block selects the mode as set_function does. Lfo::Init() leaves the phase (and other
// members) as they were: the firmware's processors are globals, zeroed at power-on, so the
// adapter zeroes them first and a reset starts from the same state every time.
void PeaksEngine::resetEngine() {
  std::memset(static_cast<void*>(&impl_->envelope), 0, sizeof(impl_->envelope));
  std::memset(static_cast<void*>(&impl_->lfo), 0, sizeof(impl_->lfo));
  impl_->envelope.Init();
  impl_->lfo.Init();
  mode_ = -1;
}

void PeaksEngine::renderBlock(const ParamValues& p, const uint8_t* gate, const double*, double, bool,
                              float (*block)[kMaxNativeBlock]) {
  Impl& s = *impl_;
  const int mode = modeIndex(p.p[0]);
  if (mode != mode_) {  // Processors::set_function (peaks/processors.h:111-119)
    s.lfo.set_sync(mode == TapLfo);
    if (mode == Envelope) {
      s.envelope.Init();
    } else if (mode != TapLfo) {
      s.lfo.Init();
    }
    mode_ = mode;
  }
  uint16_t k[4];
  knobs(mode, p, k);
  if (mode == Envelope) {
    s.envelope.Configure(k, peaks::CONTROL_MODE_FULL);
    s.envelope.Process(gate, s.out, kNativeBlock);
    for (int i = 0; i < kNativeBlock; ++i) block[0][i] = float(s.out[i]) / 32767.0f;
  } else {
    s.lfo.Configure(k, peaks::CONTROL_MODE_FULL);
    s.lfo.Process(gate, s.out, kNativeBlock);
    // The LFO's level 40960 swings +-20480 (lfo.h set_level, lfo.cc Process): the module's +-5 V.
    for (int i = 0; i < kNativeBlock; ++i) block[0][i] = std::clamp(float(s.out[i]) / 20480.0f, -1.0f, 1.0f);
  }
}

}  // namespace eg13
