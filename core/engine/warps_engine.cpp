#include "engine/warps_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "engine/param_format.h"
#include "warps/dsp/modulator.h"

namespace eg13 {

namespace {

// What the firmware does at each eighth of the knob. 0..5: the cross-modulation table
// (modulator.cc:438-445; names after XmodAlgorithm, modulator.h). 6..8: the vocoder (from 0.725,
// modulator.cc:205-207), its envelope followers' release lengthening with 4 * (a - 0.75)
// (modulator.cc:314-318; 80 Hz at 6/8, about 3.5 Hz at 7/8, vocoder.cc:70) until they freeze at
// the end (release > 0.995, vocoder.cc:75), as the module's manual describes.
const char* const kAlgorithmNames[WarpsEngine::kNumAlgorithms] = {
    "Crossfade", "Fold",    "Analog RM",
    "Digital RM", "XOR",    "Compare",
    "Vocoder",    "Vocoder (long release)", "Vocoder (frozen)"};
// Append-only (see kSelectorSteps): never reorder, only add at the end.
const char* const kCarrierNames[WarpsEngine::kNumCarriers] = {"Internal Osc", "Voice", "Source AUX", "Input",
                                                              "Other part"};
const char* const kModulatorNames[WarpsEngine::kNumModulators] = {"Voice", "Source AUX", "Input", "Other part"};
// The button's three modes name the oscillator's waveform in the cross-modulation and vocoder
// regions (carrier_shape - 1 and + 1, modulator.cc:233-237; OscillatorShape, parameters.h:35-41).
const char* const kModeShapeNames[WarpsEngine::kNumOscillatorModes] = {"Sine / Saw", "Triangle / Pulse",
                                                                      "Saw / Noise"};
const char* const kModeNames[2] = {"Modulation", "Freq shifter"};

// The knob is continuous (the sound blends between positions); the text names the nearest of
// the nine positions.
void formatAlgorithm(float v, char* buf, size_t size) {
  const int nearest = std::min(int(std::lround(clamp01(v) * 8.0f)), WarpsEngine::kNumAlgorithms - 1);
  std::snprintf(buf, size, "%s", kAlgorithmNames[nearest]);
}
void formatCarrier(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%s", kCarrierNames[WarpsEngine::carrierIndex(v)]);
}
void formatModulator(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%s", kModulatorNames[WarpsEngine::modulatorIndex(v)]);
}
void formatPitch(float v, char* buf, size_t size) { formatSemitones(WarpsEngine::oscillatorOffset(v), buf, size); }
void formatShape(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%s", kModeShapeNames[WarpsEngine::oscillatorMode(v)]);
}
void formatMode(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%s", kModeNames[WarpsEngine::shifterMode(v) ? 1 : 0]);
}

// Defaults: Timbre 0.5 as VCV (Warps.cpp:44). Algorithm on the analog ring modulator (2/8), the
// engine's namesake (VCV starts on crossfade, Warps.cpp:43). Levels at the measured unity point
// (VCV 1.0, Warps.cpp:46-47). Carrier Internal Osc and Modulator Voice: the common hardware patch,
// a signal into MODULATOR with the internal oscillator on (owner, 2026-10-20, provisional). These
// defaults live only here: changing one is a one-line edit that affects new patches only.
const EngineParamInfo kParams[9] = {
    {"Algorithm", 2.0f / 8.0f, formatAlgorithm},
    {"Timbre", 0.5f, formatPercent01},
    {"Carrier Level", WarpsEngine::kDefaultLevel, formatPercent01},
    {"Mod Level", WarpsEngine::kDefaultLevel, formatPercent01},
    {"Carrier", 0.5f / float(WarpsEngine::kSelectorSteps), formatCarrier},
    {"Modulator", 0.5f / float(WarpsEngine::kSelectorSteps), formatModulator},
    {"Osc Pitch", 0.5f, formatPitch},
    {"Osc Shape", 0.5f / 3.0f, formatShape},
    {"Mode", 0.25f, formatMode},
};

// Side input 0 feeds CARRIER, side input 1 MODULATOR. Internal Osc needs no signal (None) and
// Voice is the engine's own input; the Voice delivers the rest (Source AUX, Input, Other part).
SideSource warpsSideSource(int input, const ParamValues& p) {
  if (input == 0) {
    const int carrier = WarpsEngine::carrierIndex(p.p[4]);
    return carrier == 0 ? SideSource::None : SideSource(carrier - 1);
  }
  return SideSource(WarpsEngine::modulatorIndex(p.p[5]));
}

const EngineInfo kInfo = {"warps", "Ring Mod / Vocoder", 9, kParams, true, false, 2, warpsSideSource};

// Firmware pot smoothing, once per 60-sample block: CvScaler::Read runs once per audio block
// (warps.cc:63-70) and BIND low-passes each pot by 0.33 * 0.08 (cv_scaler.cc:65-75, 125-128).
// Osc Pitch follows the LEVEL 1 pot's note filter, 0.1 (cv_scaler.cc:159-160). Carrier,
// Modulator, Osc Shape and Mode are stepped (the button, ui.cc). Within each block Warps interpolates
// its own parameters (ParameterInterpolator and ProcessXmod, modulator.cc:157-201).
const float kPotLp[9] = {0.0264f, 0.0264f, 0.0264f, 0.0264f, 1.0f, 1.0f, 0.1f, 1.0f, 1.0f};

void initModulator(warps::Modulator& m) {
  std::memset(static_cast<void*>(&m), 0, sizeof(m));  // Init leaves members unset (Warps.cpp:61)
  m.Init(float(WarpsEngine::kNativeRate));
}

}  // namespace

struct WarpsEngine::Impl {
  warps::Modulator modulator[2];  // left, right (the right runs only while stereo)
  warps::ShortFrame in[kNativeBlock];
  warps::ShortFrame out[kNativeBlock];
  float pending[4][kNativeBlock];  // the block being collected: voice L, voice R, carrier side, modulator side
  float ready[2][kNativeBlock];    // the last processed block, played while the next fills
};

WarpsEngine::WarpsEngine()
    : impl_(std::make_unique<Impl>()), pots_(kPotLp, kInfo.paramCount, kNativeBlock) {
  initModulator(impl_->modulator[1]);
  reset();
}

WarpsEngine::~WarpsEngine() = default;

const EngineInfo& WarpsEngine::staticInfo() { return kInfo; }

const EngineInfo& WarpsEngine::info() const { return kInfo; }

float WarpsEngine::oscillatorOffset(float normalized) { return (clamp01(normalized) - 0.5f) * 48.0f; }

int WarpsEngine::oscillatorMode(float normalized) { return stepIndex(normalized, kNumOscillatorModes); }

int WarpsEngine::carrierIndex(float normalized) {
  return std::min(stepIndex(normalized, kSelectorSteps), kNumCarriers - 1);
}

int WarpsEngine::modulatorIndex(float normalized) {
  return std::min(stepIndex(normalized, kSelectorSteps), kNumModulators - 1);
}

float WarpsEngine::optionValue(int index) { return (float(index) + 0.5f) / float(kSelectorSteps); }

float WarpsEngine::tablePosition(float normalized) { return std::min(clamp01(normalized), kMaxTablePosition); }

bool WarpsEngine::shifterMode(float normalized) { return clamp01(normalized) >= 0.5f; }

short WarpsEngine::toShort(float x) {
  const float v = x * kInputScale;
  if (v != v) return 0;  // NaN
  if (v <= -32768.0f) return -32768;
  if (v >= 32767.0f) return 32767;
  return short(int(v));  // truncation, like VCV's (int) cast (Warps.cpp:102-103)
}

float WarpsEngine::fromShort(short x) { return float(x) / kInputScale; }

float WarpsEngine::smoothedParam(int index) const {
  return index >= 0 && index < kInfo.paramCount ? pots_[index] : 0.0f;
}

void WarpsEngine::prepare(double, int) {
  link_.prepare(kNativeRate);  // the engine always runs at 96 kHz (in or out of the wrapper)
  reset();
}

void WarpsEngine::reset() {
  Impl& s = *impl_;
  initModulator(s.modulator[0]);  // modulator[1] is initialised whenever stereo starts
  std::memset(s.pending, 0, sizeof(s.pending));
  std::memset(s.ready, 0, sizeof(s.ready));
  fill_ = 0;
  pots_.reset();
  link_.reset();
}

void WarpsEngine::process(const float* const* in, float* const* out, const ParamValues& p, int n) {
  Impl& s = *impl_;
  for (int i = 0; i < n; ++i) {
    s.pending[0][fill_] = in != nullptr ? in[0][i] : 0.0f;
    s.pending[1][fill_] = in != nullptr ? in[1][i] : 0.0f;
    s.pending[2][fill_] = in != nullptr && in[2] != nullptr ? in[2][i] : 0.0f;
    s.pending[3][fill_] = in != nullptr && in[3] != nullptr ? in[3][i] : 0.0f;
    out[0][i] = s.ready[0][fill_];
    out[1][i] = s.ready[1][fill_];
    if (++fill_ == kNativeBlock) {
      renderBlock(p);
      fill_ = 0;
    }
  }
}

void WarpsEngine::renderBlock(const ParamValues& p) {
  Impl& s = *impl_;
  pots_.update(p, kNativeBlock);
  const bool shifter = shifterMode(p.p[8]);
  const int carrier = carrierIndex(p.p[4]);
  const bool internal = carrier == 0;
  const bool voiceCarries = carrier == 1;
  const bool voiceModulates = modulatorIndex(p.p[5]) == 0;
  // Warps' modulator keeps pointers into itself (filter_bank.h:70, sample_rate_converter.h:279),
  // so its state cannot be copied: the right instance starts fresh and fades in.
  if (link_.request(stereo_, false) == StereoLink::Start::ResetRight) initModulator(s.modulator[1]);
  const int instances = link_.rightRuns() ? 2 : 1;
  for (int c = 0; c < instances; ++c) {
    warps::Modulator& m = s.modulator[c];
    warps::Parameters* w = m.mutable_parameters();
    // VCV's mapping (Warps.cpp:78-95): each knob straight to its parameter.
    w->channel_drive[0] = pots_[2];  // no effect with the oscillator on (modulator.cc:215)
    w->channel_drive[1] = pots_[3];
    w->modulation_algorithm = pots_[0];
    // Kept off the ends of the lookup tables upstream reads past (tablePosition).
    w->modulation_parameter = tablePosition(pots_[1]);
    w->frequency_shift_pot = pots_[0];
    w->frequency_shift_cv = 0.0f;
    w->phase_shift = tablePosition(pots_[0]);
    // The internal oscillator's pitch: the module takes it from the LEVEL 1 pot and V/OCT
    // (cv_scaler.cc:149-173); EG13 plays it at the voice's pitch plus Osc Pitch. Warps runs at
    // its own 96 kHz here, so no rate correction (VCV adds one at other rates, Warps.cpp:97).
    w->note = note_ + oscillatorOffset(pots_[6]);
    // The button: 0 takes the carrier from the CARRIER input; 1..3 replace it with the internal
    // oscillator in one of three modes (modulator.cc:209-286; Warps.cpp:68-69). In the easter egg
    // a non-zero value selects its quadrature oscillator (modulator.cc:78-98).
    w->carrier_shape = internal ? 1 + oscillatorMode(p.p[7]) : 0;  // only Internal Osc uses P7, P8
    m.set_easter_egg(shifter);
    for (int i = 0; i < kNativeBlock; ++i) {
      const float voice = s.pending[c][i];
      // CARRIER: this channel's voice or the carrier side signal, with the oscillator off. With
      // it on, the module reads this jack as the oscillator's FM (modulator.cc:228-231) and, in
      // the easter egg, adds it to the shifted signal (:143-145); v1 has no audio-rate FM (spec
      // §6), so it is silent then.
      s.in[i].l = internal ? short(0) : toShort(voiceCarries ? voice : s.pending[2][i]);
      // MODULATOR: this channel's voice, or the modulator side signal.
      s.in[i].r = toShort(voiceModulates ? voice : s.pending[3][i]);
    }
    m.Process(s.in, s.out, kNativeBlock);
    for (int i = 0; i < kNativeBlock; ++i) s.ready[c][i] = fromShort(s.out[i].l);
  }
  link_.mix(s.ready[0], s.ready[1], s.ready[1], kNativeBlock);
}

}  // namespace eg13
