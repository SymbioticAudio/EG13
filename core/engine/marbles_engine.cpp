#include "engine/marbles_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "engine/marbles_scales.h"
#include "marbles/random/random_generator.h"
#include "marbles/random/random_stream.h"
#include "marbles/random/t_generator.h"
#include "marbles/random/x_y_generator.h"
#include "stmlib/utils/gate_flags.h"

namespace eg13 {

static_assert(int(kGateHigh) == int(stmlib::GATE_FLAG_HIGH) && int(kGateRising) == int(stmlib::GATE_FLAG_RISING) &&
                  int(kGateFalling) == int(stmlib::GATE_FLAG_FALLING),
              "EG13's gate flags are stmlib's");

namespace {

// VCV's deja-vu loop lengths (AudibleInstruments Marbles.cpp:445-454, GPL-3.0 like EG13).
const int kLoopLength[36] = {1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 5,
                             5, 6, 6, 6, 7, 7, 8, 8, 8, 10, 10, 12, 12, 12, 14, 14, 16, 16};

// Append-only (kModSelectorSteps): never reorder, only add at the end.
const char* const kTModels[MarblesEngine::kNumTModels] = {"Coin Toss", "Clusters", "Drums"};
const char* const kTRanges[MarblesEngine::kNumTRanges] = {"1x", "1/4x", "4x"};
const marbles::TGeneratorRange kTRangeValues[MarblesEngine::kNumTRanges] = {
    marbles::T_GENERATOR_RANGE_1X, marbles::T_GENERATOR_RANGE_0_25X, marbles::T_GENERATOR_RANGE_4X};
const char* const kXModes[MarblesEngine::kNumXModes] = {"Identical", "Bump", "Tilt"};
const char* const kXRanges[MarblesEngine::kNumXRanges] = {"Narrow", "Positive", "Full"};
const char* const kScales[MarblesEngine::kNumScales] = {"Major", "Minor", "Pentatonic", "Pelog", "Raag Bhairav",
                                                        "Raag Shri"};
const char* const kOffOn[2] = {"Off", "On"};

void formatTModel(float v, char* buf, size_t size) { formatOption(kTModels, 3, v, buf, size); }
void formatTRange(float v, char* buf, size_t size) { formatOption(kTRanges, 3, v, buf, size); }
void formatXMode(float v, char* buf, size_t size) { formatOption(kXModes, 3, v, buf, size); }
void formatXRange(float v, char* buf, size_t size) { formatOption(kXRanges, 3, v, buf, size); }
void formatScale(float v, char* buf, size_t size) { formatOption(kScales, 6, v, buf, size); }
void formatOffOn(float v, char* buf, size_t size) { formatOption(kOffOn, 2, v, buf, size); }
void formatRate(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%+d%%", int(std::lround((2.0f * clamp01(v) - 1.0f) * 100.0f)));
}
void formatLength(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%d steps", MarblesEngine::lengthSteps(v));
}

// Defaults are the mod slots' shared host defaults (param_table.cpp, kModHostDefaults): VCV's
// defaults (Marbles.cpp:230-242, 269-279), except X Range Full (bipolar) instead of Positive.
const EngineParamInfo kParams[16] = {
    {"T Model", 0.0f, formatTModel},
    {"T Deja Vu", 0.0f, formatOffOn},
    {"T Rate", 0.5f, formatRate},
    {"T Bias", 0.5f, formatPercent01},
    {"Deja Vu", 0.5f, formatPercent01},
    {"X Range", 1.0f, formatXRange},
    {"X Spread", 0.5f, formatPercent01},
    {"X Bias", 0.5f, formatPercent01},
    {"X Steps", 0.5f, formatPercent01},
    {"T Range", 0.0f, formatTRange},
    {"T Jitter", 0.0f, formatPercent01},
    {"Length", 0.0f, formatLength},
    {"X Mode", 0.0f, formatXMode},
    {"X Scale", 0.0f, formatScale},
    {"X Deja Vu", 0.0f, formatOffOn},
    {"Sync", 0.0f, formatOffOn},
};

const ModOutputInfo kOutputs[7] = {{"T1", false}, {"T2", false}, {"T3", false}, {"X1", true},
                                   {"X2", true},  {"X3", true},  {"Y", true}};

const ModEngineInfo kInfo = {{"marbles", "Random / S&H", 16, kParams, false}, 7, kOutputs, true};

}  // namespace

struct MarblesEngine::Impl {
  marbles::RandomGenerator generator;
  marbles::RandomStream stream;
  marbles::TGenerator t;
  marbles::XYGenerator xy;
  float rampMaster[kNativeBlock];
  float rampExternal[kNativeBlock];
  float rampSlave[2][kNativeBlock];
  bool gates[2 * kNativeBlock];
  float voltages[4 * kNativeBlock];
  stmlib::GateFlags clock = stmlib::GATE_FLAG_LOW;
};

MarblesEngine::MarblesEngine() : NativeModEngine(kNativeRate, kNativeBlock), impl_(std::make_unique<Impl>()) {
  resetEngine();
}

MarblesEngine::~MarblesEngine() = default;

const ModEngineInfo& MarblesEngine::staticInfo() { return kInfo; }

const ModEngineInfo& MarblesEngine::info() const { return kInfo; }

int MarblesEngine::lengthSteps(float normalized) {
  return kLoopLength[std::lround(clamp01(normalized) * 35.0f)];
}

int MarblesEngine::tRange(float normalized) { return int(kTRangeValues[modOption(normalized, kNumTRanges)]); }

// As VCV's Marbles does on reset and on a rate change (Marbles.cpp:262-264, 288-298): seed the
// generator, then initialise both generators at the running rate and load the six scales.
void MarblesEngine::resetEngine() {
  Impl& s = *impl_;
  s.generator.Init(seed_);
  s.stream.Init(&s.generator);
  s.t.Init(&s.stream, float(kNativeRate));
  s.xy.Init(&s.stream, float(kNativeRate));
  for (int i = 0; i < kNumScales; ++i) s.xy.LoadScale(i, marbles_scales::preset_scales[i]);
  s.clock = stmlib::GATE_FLAG_LOW;
}

void MarblesEngine::renderBlock(const ParamValues& p, const uint8_t*, const double* beat, double, bool beatJump,
                                float (*block)[kMaxNativeBlock]) {
  Impl& s = *impl_;
  const bool sync = modOption(p.p[15], 2) == 1;
  const float dejaVu = clamp01(p.p[4]);
  const int length = lengthSteps(p.p[11]);
  const int xRange = modOption(p.p[5], kNumXRanges);

  // T section (Marbles.cpp:458-476).
  s.t.set_model(marbles::TGeneratorModel(modOption(p.p[0], kNumTModels)));
  s.t.set_range(marbles::TGeneratorRange(tRange(p.p[9])));
  s.t.set_rate(60.0f * (2.0f * clamp01(p.p[2]) - 1.0f));
  s.t.set_bias(clamp01(p.p[3]));
  s.t.set_jitter(clamp01(p.p[10]));
  s.t.set_deja_vu(modOption(p.p[1], 2) == 1 ? dejaVu : 0.0f);
  s.t.set_length(length);
  s.t.set_pulse_width_mean(0.0f);
  s.t.set_pulse_width_std(0.0f);
  marbles::Ramps ramps;
  ramps.master = s.rampMaster;
  ramps.external = s.rampExternal;
  ramps.slave[0] = s.rampSlave[0];
  ramps.slave[1] = s.rampSlave[1];
  // Sync: one clock pulse per beat into T's clock input, high for the first half of each beat.
  stmlib::GateFlags clock[kNativeBlock];
  for (int i = 0; i < kNativeBlock; ++i) {
    s.clock = clock[i] = stmlib::ExtractGateFlags(s.clock, beat[i] - std::floor(beat[i]) < 0.5);
  }
  bool tReset = sync && beatJump;  // the transport started or jumped: start the pattern again
  s.t.Process(sync, &tReset, clock, ramps, s.gates, kNativeBlock);

  // X and Y sections (Marbles.cpp:480-534): X clocked by T, Y following X's settings.
  marbles::GroupSettings x;
  x.control_mode = marbles::ControlMode(modOption(p.p[12], kNumXModes));
  x.voltage_range = marbles::VoltageRange(xRange);
  x.register_mode = false;
  x.register_value = 0.0f;
  x.spread = clamp01(p.p[6]);
  x.bias = clamp01(p.p[7]);
  x.steps = clamp01(p.p[8]);
  x.deja_vu = modOption(p.p[14], 2) == 1 ? dejaVu : 0.0f;
  x.scale_index = modOption(p.p[13], kNumScales);
  x.length = length;
  x.ratio.p = 1;
  x.ratio.q = 1;
  marbles::GroupSettings y = x;
  y.control_mode = marbles::CONTROL_MODE_IDENTICAL;
  y.deja_vu = 0.0f;
  y.length = 1;
  y.ratio.p = 1;  // VCV's default Y divider (y_divider_index 8, Marbles.cpp:278, 517-531)
  y.ratio.q = 4;
  bool xyReset = sync && beatJump;
  s.xy.Process(marbles::CLOCK_SOURCE_INTERNAL_T1_T2_T3, x, y, &xyReset, clock, ramps, s.voltages, kNativeBlock);

  // Volts to EG13's range by X Range (x_y_generator.cc:134-145): 0..2 V, 0..5 V, -5..+5 V.
  const float scale = xRange == marbles::VOLTAGE_RANGE_NARROW ? 0.5f : 0.2f;
  for (int i = 0; i < kNativeBlock; ++i) {
    block[0][i] = s.gates[2 * i] ? 1.0f : 0.0f;
    block[1][i] = s.rampMaster[i] < 0.5f ? 1.0f : 0.0f;  // T2 is the master clock (Marbles.cpp:420)
    block[2][i] = s.gates[2 * i + 1] ? 1.0f : 0.0f;
    for (int k = 0; k < 4; ++k) block[3 + k][i] = std::clamp(s.voltages[4 * i + k] * scale, -1.0f, 1.0f);
  }
}

}  // namespace eg13
