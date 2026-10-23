#include "engine/tides_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "stmlib/dsp/hysteresis_quantizer.h"
#include "stmlib/dsp/units.h"
#include "stmlib/utils/gate_flags.h"
#include "tides2/poly_slope_generator.h"

namespace eg13 {

static_assert(int(kGateHigh) == int(stmlib::GATE_FLAG_HIGH) && int(kGateRising) == int(stmlib::GATE_FLAG_RISING) &&
                  int(kGateFalling) == int(stmlib::GATE_FLAG_FALLING),
              "EG13's gate flags are stmlib's");

namespace {

// The clocked mode's ratios, tides2/tides.cc:108-129 (a file that cannot be compiled here).
const tides::Ratio kRatios[20] = {
    {0.0625f, 16}, {0.125f, 8}, {0.1666666f, 6}, {0.25f, 4}, {0.3333333f, 3}, {0.5f, 2}, {0.6666666f, 3},
    {0.75f, 4},    {0.8f, 5},   {1.0f, 1},       {1.0f, 1},  {1.25f, 4},      {1.3333333f, 3}, {1.5f, 2},
    {2.0f, 1},     {3.0f, 1},   {4.0f, 1},       {6.0f, 1},  {8.0f, 1},       {16.0f, 1},
};

// Append-only (kModSelectorSteps): never reorder, only add at the end.
const char* const kModeNames[TidesEngine::kNumModes] = {"Cycle", "AD", "AR"};
const tides::RampMode kModes[TidesEngine::kNumModes] = {tides::RAMP_MODE_LOOPING, tides::RAMP_MODE_AD,
                                                       tides::RAMP_MODE_AR};
const char* const kOutputNames[TidesEngine::kNumOutputModes] = {"Phase", "Gates", "Amplitude", "Frequency"};
const tides::OutputMode kOutputs[TidesEngine::kNumOutputModes] = {
    tides::OUTPUT_MODE_SLOPE_PHASE, tides::OUTPUT_MODE_GATES, tides::OUTPUT_MODE_AMPLITUDE,
    tides::OUTPUT_MODE_FREQUENCY};
const char* const kRangeNames[TidesEngine::kNumRanges] = {"Medium", "Low"};

void formatMode(float v, char* buf, size_t size) { formatOption(kModeNames, TidesEngine::kNumModes, v, buf, size); }
void formatSync(float v, char* buf, size_t size) { std::snprintf(buf, size, "%s", TidesEngine::syncOn(v) ? "On" : "Off"); }
void formatOutput(float v, char* buf, size_t size) {
  formatOption(kOutputNames, TidesEngine::kNumOutputModes, v, buf, size);
}
void formatRange(float v, char* buf, size_t size) { formatOption(kRangeNames, TidesEngine::kNumRanges, v, buf, size); }
void formatFrequency(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%+.1f st", double(TidesEngine::transposition(v)));
}

// Defaults are the mod slots' shared host defaults (param_table.cpp, kModHostDefaults): cycling
// at 2 Hz (Medium, 0 st), the phase outputs a quarter cycle apart (Shift 100 %), neutral shape,
// slope and smoothness.
const EngineParamInfo kParams[11] = {
    {"Mode", 0.0f, formatMode},
    {"Sync", 0.0f, formatSync},
    {"Frequency", 0.5f, formatFrequency},
    {"Shape", 0.5f, formatPercent01},
    {"Slope", 0.5f, formatPercent01},
    {"Shift", 1.0f, formatPercent01},
    {"Smoothness", 0.5f, formatPercent01},
    {"(unused)", 0.5f, nullptr},
    {"(unused)", 0.5f, nullptr},
    {"Output", 0.0f, formatOutput},
    {"Range", 0.0f, formatRange},
};

const ModOutputInfo kOutputInfo[4] = {{"Out 1", true}, {"Out 2", true}, {"Out 3", true}, {"Out 4", true}};

const ModEngineInfo kInfo = {{"tides", "LFO", 11, kParams, false}, 4, kOutputInfo, false};

}  // namespace

struct TidesEngine::Impl {
  tides::PolySlopeGenerator generator;
  stmlib::HysteresisQuantizer2 ratioQuantizer;
  tides::PolySlopeGenerator::OutputSample out[kNativeBlock];
  stmlib::GateFlags noGate[kNativeBlock] = {};
  int previousOutput = -1;
};

TidesEngine::TidesEngine() : NativeModEngine(kNativeRate, kNativeBlock), impl_(std::make_unique<Impl>()) {
  resetEngine();
}

TidesEngine::~TidesEngine() = default;

const ModEngineInfo& TidesEngine::staticInfo() { return kInfo; }

const ModEngineInfo& TidesEngine::info() const { return kInfo; }

int TidesEngine::modeIndex(float normalized) { return modOption(normalized, kNumModes); }

bool TidesEngine::syncOn(float normalized) { return modOption(normalized, 2) == 1; }

float TidesEngine::transposition(float normalized) { return (clamp01(normalized) - 0.5f) * 96.0f; }

int TidesEngine::outputIndex(float normalized) { return modOption(normalized, kNumOutputModes); }

int TidesEngine::rangeIndex(float normalized) { return modOption(normalized, kNumRanges); }

float TidesEngine::rootHz(int range) { return range == Low ? 0.125f : 2.0f; }

float TidesEngine::frequencyFor(double hz, int range) {
  const double semitones = 12.0 * std::log2(hz / double(rootHz(range)));
  return clamp01(float(0.5 + semitones / 96.0));
}

void TidesEngine::resetEngine() {
  impl_->generator.Init();
  impl_->ratioQuantizer.Init(20, 0.05f, false);  // tides.cc:265
  impl_->previousOutput = -1;
}

void TidesEngine::renderBlock(const ParamValues& p, const uint8_t* gate, const double* beat, double beatsPerNative,
                              bool, float (*block)[kMaxNativeBlock]) {
  Impl& s = *impl_;
  const tides::RampMode mode = kModes[modeIndex(p.p[0])];
  const bool sync = syncOn(p.p[1]);
  const float transpose = transposition(p.p[2]);
  const tides::OutputMode output = kOutputs[outputIndex(p.p[9])];
  const bool half = output >= tides::OUTPUT_MODE_SLOPE_PHASE;  // tides.cc:145
  const int size = half ? kNativeBlock / 2 : kNativeBlock;
  stmlib::GateFlags flags[kNativeBlock];
  for (int i = 0; i < size; ++i) flags[i] = half ? gate[2 * i] : gate[i];  // tides.cc:165-171

  float ramp[kNativeBlock];
  float frequency;
  if (sync) {
    // The clocked mode's ratio (tides.cc:186-187) applied to the beat position: one cycle per beat
    // at ratio 1, phase-locked to the host's beats.
    const tides::Ratio& r = s.ratioQuantizer.Lookup(kRatios, 0.5f + transpose * 0.0105f);
    for (int i = 0; i < size; ++i) {
      const double b = beat[half ? 2 * i : i] * double(r.ratio);
      ramp[i] = float(b - std::floor(b));
    }
    frequency = float(beatsPerNative * double(r.ratio)) * (half ? 2.0f : 1.0f);
  } else {
    frequency = rootHz(rangeIndex(p.p[10])) / float(kNativeRate) * stmlib::SemitonesToRatio(transpose);
    if (half) frequency *= 2.0f;  // tides.cc:197-200
  }
  if (int(output) != s.previousOutput) {  // tides.cc:204-207
    s.generator.Reset();
    s.previousOutput = int(output);
  }
  s.generator.Render(mode, output, tides::RANGE_CONTROL, frequency, clamp01(p.p[4]), clamp01(p.p[3]),
                     clamp01(p.p[6]), clamp01(p.p[5]), sync ? s.noGate : flags, sync ? ramp : nullptr, s.out,
                     size_t(size));
  // Volts to EG13's -1..1: +-5 V when cycling, 0..8 V for AD and AR (poly_slope_generator.h Fold
  // and Scale); the end-of-attack and end-of-release gates (8 V) clamp to 1.
  const float scale = mode == tides::RAMP_MODE_LOOPING ? 0.2f : 0.125f;
  for (int i = 0; i < kNativeBlock; ++i) {
    const tides::PolySlopeGenerator::OutputSample& o = s.out[half ? i / 2 : i];
    for (int k = 0; k < 4; ++k) block[k][i] = std::clamp(o.channel[k] * scale, -1.0f, 1.0f);
  }
}

}  // namespace eg13
