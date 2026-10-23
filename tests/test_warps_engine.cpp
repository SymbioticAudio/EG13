#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "alloc_counter.h"
#include "engine/native_rate_wrapper.h"
#include "engine/warps_engine.h"
#include "stmlib/utils/random.h"
#include "test_helpers.h"
#include "warps/dsp/modulator.h"

using namespace eg13;

namespace {

constexpr double kRate = WarpsEngine::kNativeRate;
constexpr int kN = WarpsEngine::kNativeBlock;

ParamValues defaults() { return test::defaultParams(WarpsEngine::staticInfo()); }

// Carrier and Modulator options (eighths, append-only).
constexpr float kCarrierOsc = 0.5f / 8.0f;
constexpr float kCarrierVoice = 1.5f / 8.0f;
constexpr float kCarrierAux = 2.5f / 8.0f;
constexpr float kCarrierInput = 3.5f / 8.0f;
constexpr float kCarrierOther = 4.5f / 8.0f;
constexpr float kModVoice = 0.5f / 8.0f;
constexpr float kModAux = 1.5f / 8.0f;
constexpr float kModInput = 2.5f / 8.0f;
constexpr float kModOther = 3.5f / 8.0f;

std::vector<float> sine(size_t n, double hz, double rate, float amp = 0.5f) {
  std::vector<float> x(n);
  for (size_t i = 0; i < n; ++i) x[i] = amp * float(std::sin(6.283185307179586 * hz * double(i) / rate));
  return x;
}

struct Stereo {
  std::vector<float> l, r;
};

// carrierSide / modulatorSide: what the Voice would deliver to the side inputs (nullable).
Stereo run(VoiceEngine& e, const std::vector<float>& voiceL, const std::vector<float>& voiceR,
           const std::vector<float>* carrierSide, const std::vector<float>* modulatorSide, const ParamValues& p,
           int chunk = kN) {
  Stereo s{std::vector<float>(voiceL.size()), std::vector<float>(voiceL.size())};
  for (size_t done = 0; done < voiceL.size();) {
    const int count = int(std::min<size_t>(size_t(chunk), voiceL.size() - done));
    const float* in[4] = {voiceL.data() + done, voiceR.data() + done,
                          carrierSide ? carrierSide->data() + done : nullptr,
                          modulatorSide ? modulatorSide->data() + done : nullptr};
    float* out[2] = {s.l.data() + done, s.r.data() + done};
    e.process(in, out, p, count);
    done += size_t(count);
  }
  return s;
}

struct Settings {
  float algorithm, timbre, level1, level2;
};

// Upstream Warps driven as the adapter drives it, block by block: carrierJack into channel 1
// (CARRIER), modulatorJack into channel 2 (MODULATOR), the internal-oscillator button as
// carrierShape (0 = off), the easter egg as shifter.
std::vector<float> reference(const std::vector<float>& carrierJack, const std::vector<float>& modulatorJack,
                             const Settings& s, int carrierShape, bool shifter, float note = 60.0f) {
  auto m = std::make_unique<warps::Modulator>();
  std::memset(static_cast<void*>(m.get()), 0, sizeof(warps::Modulator));
  m->Init(float(kRate));
  std::vector<float> out(carrierJack.size());
  warps::ShortFrame in[kN], o[kN];
  for (size_t b = 0; b + kN <= carrierJack.size(); b += kN) {
    warps::Parameters* w = m->mutable_parameters();
    w->channel_drive[0] = s.level1;
    w->channel_drive[1] = s.level2;
    w->modulation_algorithm = s.algorithm;
    w->modulation_parameter = s.timbre;
    w->frequency_shift_pot = s.algorithm;
    w->frequency_shift_cv = 0.0f;
    w->phase_shift = s.algorithm;
    w->note = note;
    w->carrier_shape = carrierShape;
    m->set_easter_egg(shifter);
    for (int i = 0; i < kN; ++i) {
      in[i].l = WarpsEngine::toShort(carrierJack[b + size_t(i)]);
      in[i].r = WarpsEngine::toShort(modulatorJack[b + size_t(i)]);
    }
    m->Process(in, o, kN);
    for (int i = 0; i < kN; ++i) out[b + size_t(i)] = WarpsEngine::fromShort(o[i].l);
  }
  return out;
}

ParamValues with(const Settings& s, float carrier, float modulator, bool shifter) {
  ParamValues p = defaults();
  p.p[0] = s.algorithm;
  p.p[1] = s.timbre;
  p.p[2] = s.level1;
  p.p[3] = s.level2;
  p.p[4] = carrier;
  p.p[5] = modulator;
  p.p[8] = shifter ? 0.75f : 0.25f;
  return p;
}

double magnitudeAt(const std::vector<float>& x, double rate, size_t begin, size_t end, double f) {
  const double pi = 3.14159265358979323846;
  const size_t count = end - begin;
  const double coeff = 2.0 * std::cos(2.0 * pi * f / rate);
  double s1 = 0.0, s2 = 0.0;
  for (size_t i = 0; i < count; ++i) {
    const double hann = 0.5 - 0.5 * std::cos(2.0 * pi * double(i) / double(count));
    const double s0 = double(x[begin + i]) * hann + coeff * s1 - s2;
    s2 = s1;
    s1 = s0;
  }
  return std::sqrt(std::max(0.0, s1 * s1 + s2 * s2 - coeff * s1 * s2));
}

}  // namespace

TEST_CASE("Ring Mod / Vocoder describes itself") {
  const auto& info = WarpsEngine::staticInfo();
  REQUIRE(std::string(info.id) == "warps");
  REQUIRE(std::string(info.displayName) == "Ring Mod / Vocoder");
  REQUIRE(info.paramCount == 9);
  REQUIRE(info.usesInput);
  REQUIRE(info.sideInputs == 2);
  REQUIRE(info.sideSource != nullptr);
  const char* const names[9] = {"Algorithm", "Timbre",    "Carrier Level", "Mod Level", "Carrier",
                                "Modulator", "Osc Pitch", "Osc Shape",     "Mode"};
  for (int i = 0; i < 9; ++i) REQUIRE(std::string(info.params[i].name) == names[i]);
  REQUIRE(info.params[2].defaultValue == WarpsEngine::kDefaultLevel);
  REQUIRE(info.params[3].defaultValue == WarpsEngine::kDefaultLevel);
  REQUIRE(WarpsEngine::carrierIndex(info.params[4].defaultValue) == 0);    // Internal Osc
  REQUIRE(WarpsEngine::modulatorIndex(info.params[5].defaultValue) == 0);  // Voice
  REQUIRE(WarpsEngine::oscillatorMode(info.params[7].defaultValue) == 0);
  REQUIRE_FALSE(WarpsEngine::shifterMode(info.params[8].defaultValue));
  char buf[64];
  info.params[0].format(info.params[0].defaultValue, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Analog RM");
  info.params[0].format(1.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Vocoder (frozen)");
  // The nine positions, named after what the firmware does there; the text names the nearest.
  const char* const algorithms[9] = {"Crossfade",  "Fold",    "Analog RM",
                                     "Digital RM", "XOR",     "Compare",
                                     "Vocoder",    "Vocoder (long release)", "Vocoder (frozen)"};
  for (int i = 0; i < 9; ++i) {
    for (float offset : {-0.45f, 0.0f, 0.45f}) {
      const float v = std::clamp((float(i) + offset) / 8.0f, 0.0f, 1.0f);
      info.params[0].format(v, buf, sizeof(buf));
      INFO("position " << i << " offset " << offset);
      REQUIRE(std::string(buf) == algorithms[i]);
    }
  }
  info.params[4].format(info.params[4].defaultValue, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Internal Osc");
  info.params[5].format(info.params[5].defaultValue, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Voice");
  info.params[6].format(0.75f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "+12.0 st");
  info.params[7].format(2.5f / 3.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Saw / Noise");
  info.params[8].format(0.75f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Freq shifter");
  REQUIRE(WarpsEngine::toShort(1.0f) == 10240);  // ±1.0 = ±5 V of Warps' ±8 V inputs
  REQUIRE(WarpsEngine::fromShort(10240) == 1.0f);
  REQUIRE(WarpsEngine::toShort(10.0f) == 32767);
  REQUIRE(WarpsEngine::toShort(-10.0f) == -32768);
  REQUIRE(WarpsEngine::toShort(std::numeric_limits<float>::quiet_NaN()) == 0);
}

// The Carrier and Modulator option lists are append-only: saved projects store the selector
// value, so these orders and the 8-step reading must never change (owner, 2026-10-20: the routing
// is provisional; options may be added, never moved).
TEST_CASE("Ring Mod / Vocoder Carrier and Modulator options keep their order") {
  const auto& info = WarpsEngine::staticInfo();
  REQUIRE(WarpsEngine::kSelectorSteps == 8);
  const char* const carriers[5] = {"Internal Osc", "Voice", "Source AUX", "Input", "Other part"};
  const SideSource carrierSources[5] = {SideSource::None, SideSource::Voice, SideSource::SourceAux,
                                        SideSource::Input, SideSource::OtherPart};
  const char* const modulators[4] = {"Voice", "Source AUX", "Input", "Other part"};
  char buf[64];
  ParamValues p = defaults();
  for (int i = 0; i < 5; ++i) {
    const float v = WarpsEngine::optionValue(i);
    REQUIRE(v == (float(i) + 0.5f) / 8.0f);
    REQUIRE(WarpsEngine::carrierIndex(v) == i);
    info.params[4].format(v, buf, sizeof(buf));
    REQUIRE(std::string(buf) == carriers[i]);
    p.p[4] = v;
    REQUIRE(info.sideSource(0, p) == carrierSources[i]);
  }
  for (int i = 0; i < 4; ++i) {
    const float v = WarpsEngine::optionValue(i);
    REQUIRE(WarpsEngine::modulatorIndex(v) == i);
    info.params[5].format(v, buf, sizeof(buf));
    REQUIRE(std::string(buf) == modulators[i]);
    p.p[5] = v;
    REQUIRE(info.sideSource(1, p) == SideSource(i));
  }
  REQUIRE(WarpsEngine::carrierIndex(std::numeric_limits<float>::quiet_NaN()) == 0);
}

// Pins today's reading of the unused steps: each, and 1.0, reads as the last option. When an
// option is appended, the state migration must snap saved values above the old last option's
// step back to its centre (rule at WarpsEngine::kSelectorSteps), and this test changes with it.
TEST_CASE("Ring Mod / Vocoder unused selector steps read as the last option") {
  for (int step = WarpsEngine::kNumCarriers; step < WarpsEngine::kSelectorSteps; ++step) {
    for (float within : {0.0f, 0.5f, 0.999f}) {
      const float v = (float(step) + within) / float(WarpsEngine::kSelectorSteps);
      INFO("carrier step " << step << " value " << v);
      REQUIRE(WarpsEngine::carrierIndex(v) == WarpsEngine::kNumCarriers - 1);
    }
  }
  for (int step = WarpsEngine::kNumModulators; step < WarpsEngine::kSelectorSteps; ++step) {
    for (float within : {0.0f, 0.5f, 0.999f}) {
      const float v = (float(step) + within) / float(WarpsEngine::kSelectorSteps);
      INFO("modulator step " << step << " value " << v);
      REQUIRE(WarpsEngine::modulatorIndex(v) == WarpsEngine::kNumModulators - 1);
    }
  }
  REQUIRE(WarpsEngine::carrierIndex(1.0f) == WarpsEngine::kNumCarriers - 1);
  REQUIRE(WarpsEngine::modulatorIndex(1.0f) == WarpsEngine::kNumModulators - 1);
}

// The default Carrier Level and Mod Level (owner, 2026-10-20): the drive at which Warps'
// SaturatingAmplifier (warps/dsp/modulator.h:53-115) passes a full-scale EG13 voice, a 1 kHz sine
// at ±1.0 (±10240 counts, the adapter's input scaling), at unity gain without saturating. Method:
// drive the upstream amplifier alone with limit 1 (cross-modulation), measure the fundamental and
// harmonics 2..9 over 0.5 s after 0.5 s of settling. Planning measured +0.03 dB and 0.06 % THD at
// 0.47 (+0.22 dB at 0.48, -0.17 dB at 0.46); 1.0 gives +12 dB and 38 % THD.
TEST_CASE("Default levels pass a full-scale voice through Warps' amplifier at unity, unsaturated") {
  auto measure = [](float drive, double* gainDb, double* thd) {
    warps::SaturatingAmplifier amplifier;
    std::memset(static_cast<void*>(&amplifier), 0, sizeof(amplifier));
    amplifier.Init();
    const int n = 96000;
    std::vector<float> y(static_cast<size_t>(n));
    short in[kN];
    float out[kN], raw[kN];
    for (int b = 0; b < n / kN; ++b) {
      for (int i = 0; i < kN; ++i) {
        in[i] = WarpsEngine::toShort(float(std::sin(6.283185307179586 * 1000.0 * double(b * kN + i) / kRate)));
      }
      std::memset(raw, 0, sizeof(raw));
      amplifier.Process(drive, 1.0f, in, out, raw, 1, kN);
      for (int i = 0; i < kN; ++i) y[size_t(b * kN + i)] = out[i];
    }
    auto amplitude = [&](double f) { return 2.0 * magnitudeAt(y, kRate, 48000, 96000, f) / 24000.0; };
    const double fundamental = amplitude(1000.0);
    double harmonics = 0.0;
    for (int h = 2; h <= 9; ++h) harmonics += amplitude(1000.0 * h) * amplitude(1000.0 * h);
    *gainDb = 20.0 * std::log10(fundamental / (10240.0 / 32768.0));
    *thd = std::sqrt(harmonics) / fundamental;
  };
  double gain = 0.0, thd = 0.0;
  measure(WarpsEngine::kDefaultLevel, &gain, &thd);
  INFO("default " << WarpsEngine::kDefaultLevel << ": " << gain << " dB, THD " << 100.0 * thd << " %");
  REQUIRE(std::fabs(gain) < 0.25);
  REQUIRE(thd < 0.001);
  measure(1.0f, &gain, &thd);  // the range still reaches the saturated character
  REQUIRE(thd > 0.1);
}

TEST_CASE("Ring Mod / Vocoder with an external carrier matches upstream Warps bit for bit, one block late") {
  const auto voice = sine(kN * 200, 440.0, kRate);
  const auto side = sine(kN * 200, 130.0, kRate, 0.4f);
  for (float algorithm : {0.0f, 0.25f, 0.4f, 0.5f, 0.6f, 0.72f, 0.8f, 1.0f}) {
    const Settings s{algorithm, 0.3f, 0.7f, 0.9f};
    WarpsEngine engine;
    engine.prepare(kRate, 256);
    const Stereo out = run(engine, voice, voice, nullptr, &side, with(s, kCarrierVoice, kModInput, false));
    const auto expected = reference(voice, side, s, 0, false);  // voice on CARRIER, side on MODULATOR
    INFO("algorithm " << algorithm);
    for (size_t i = 0; i < size_t(kN); ++i) REQUIRE(out.l[i] == 0.0f);
    for (size_t i = 0; i + kN < out.l.size(); ++i) REQUIRE(out.l[i + kN] == expected[i]);
  }
}

TEST_CASE("Each Carrier option drives Warps' carrier input") {
  const auto voice = sine(kN * 100, 440.0, kRate);
  const auto carrierSide = sine(kN * 100, 700.0, kRate, 0.3f);
  const auto modulatorSide = sine(kN * 100, 130.0, kRate, 0.4f);
  const std::vector<float> silent(voice.size(), 0.0f);
  const Settings s{0.375f, 0.2f, 0.6f, 0.6f};  // digital ring modulation
  struct Case {
    float carrier;
    const std::vector<float>* jack;  // what must reach CARRIER
    int carrierShape;
  } cases[] = {{kCarrierOsc, &silent, 1},       {kCarrierVoice, &voice, 0},        {kCarrierAux, &carrierSide, 0},
               {kCarrierInput, &carrierSide, 0}, {kCarrierOther, &carrierSide, 0}};
  for (const Case& c : cases) {
    WarpsEngine engine;
    engine.prepare(kRate, 256);
    engine.noteOn(57.0f, 1.0f);
    const Stereo out = run(engine, voice, voice, &carrierSide, &modulatorSide, with(s, c.carrier, kModInput, false));
    const auto expected = reference(*c.jack, modulatorSide, s, c.carrierShape, false, 57.0f);
    INFO("carrier option " << WarpsEngine::carrierIndex(c.carrier));
    for (size_t i = 0; i + kN < out.l.size(); ++i) REQUIRE(out.l[i + kN] == expected[i]);
  }
}

TEST_CASE("Each Modulator option drives Warps' modulator input") {
  const auto voice = sine(kN * 100, 440.0, kRate);
  const auto modulatorSide = sine(kN * 100, 130.0, kRate, 0.4f);
  const Settings s{0.375f, 0.2f, 0.6f, 0.6f};
  struct Case {
    float modulator;
    const std::vector<float>* jack;  // what must reach MODULATOR
  } cases[] = {{kModVoice, &voice}, {kModAux, &modulatorSide}, {kModInput, &modulatorSide}, {kModOther, &modulatorSide}};
  for (const Case& c : cases) {
    WarpsEngine engine;
    engine.prepare(kRate, 256);
    const Stereo out = run(engine, voice, voice, nullptr, &modulatorSide, with(s, kCarrierVoice, c.modulator, false));
    const auto expected = reference(voice, *c.jack, s, 0, false);  // Voice + Voice: self-modulation
    INFO("modulator option " << WarpsEngine::modulatorIndex(c.modulator));
    for (size_t i = 0; i + kN < out.l.size(); ++i) REQUIRE(out.l[i + kN] == expected[i]);
  }
}

TEST_CASE("Ring Mod / Vocoder's Internal Osc carrier is Warps' oscillator at the note plus Osc Pitch") {
  const auto voice = sine(kN * 120, 220.0, kRate);
  for (int mode = 0; mode < WarpsEngine::kNumOscillatorModes; ++mode) {
    for (float algorithm : {0.25f, 0.6f, 0.8f, 1.0f}) {
      const Settings s{algorithm, 0.4f, 0.8f, 0.6f};
      ParamValues p = with(s, kCarrierOsc, kModVoice, false);
      p.p[6] = 0.75f;                           // +12 st
      p.p[7] = (float(mode) + 0.5f) / 3.0f;
      WarpsEngine engine;
      engine.prepare(kRate, 256);
      engine.noteOn(57.0f, 1.0f);
      // The vocoder's noise carrier (mode 3) draws from stmlib's per-thread generator: both
      // renders start from the same seed.
      stmlib::Random::Seed(0x21);
      const Stereo out = run(engine, voice, voice, nullptr, nullptr, p);
      // CARRIER silent (it would be the oscillator's FM), MODULATOR = the voice, button mode + 1.
      const std::vector<float> silent(voice.size(), 0.0f);
      stmlib::Random::Seed(0x21);
      const auto expected = reference(silent, voice, s, 1 + mode, false, 57.0f + 12.0f);
      INFO("mode " << mode << " algorithm " << algorithm);
      for (size_t i = 0; i + kN < out.l.size(); ++i) REQUIRE(out.l[i + kN] == expected[i]);
    }
  }
}

TEST_CASE("Ring Mod / Vocoder frequency shifter matches Warps' easter egg, internal and external carrier") {
  const auto voice = sine(kN * 200, 1000.0, kRate);
  const auto side = sine(kN * 200, 300.0, kRate, 0.4f);
  const Settings s{0.9f, 0.6f, 0.2f, 0.8f};
  const std::vector<float> silent(voice.size(), 0.0f);
  {
    WarpsEngine engine;
    engine.prepare(kRate, 256);
    const Stereo out = run(engine, voice, voice, nullptr, nullptr, with(s, kCarrierOsc, kModVoice, true));
    const auto expected = reference(silent, voice, s, 1, true);
    for (size_t i = 0; i + kN < out.l.size(); ++i) REQUIRE(out.l[i + kN] == expected[i]);
  }
  {
    WarpsEngine engine;
    engine.prepare(kRate, 256);
    const Stereo out = run(engine, voice, voice, nullptr, &side, with(s, kCarrierVoice, kModInput, true));
    const auto expected = reference(voice, side, s, 0, true);
    for (size_t i = 0; i + kN < out.l.size(); ++i) REQUIRE(out.l[i + kN] == expected[i]);
  }
}

TEST_CASE("Ring Mod / Vocoder digital ring modulation makes sum and difference tones") {
  const auto voice = sine(96000, 1000.0, kRate);
  const auto side = sine(96000, 300.0, kRate);
  WarpsEngine engine;
  engine.prepare(kRate, 256);
  const Stereo out = run(engine, voice, voice, nullptr, &side, with({0.375f, 0.0f, 0.5f, 0.5f}, kCarrierVoice, kModInput, false));
  const double sum = magnitudeAt(out.l, kRate, 9600, 96000, 1300.0);
  const double difference = magnitudeAt(out.l, kRate, 9600, 96000, 700.0);
  const double carrierLeak = magnitudeAt(out.l, kRate, 9600, 96000, 1000.0);
  INFO("1300 Hz " << sum << " 700 Hz " << difference << " 1000 Hz " << carrierLeak);
  REQUIRE(sum > 10.0 * carrierLeak);
  REQUIRE(difference > 10.0 * carrierLeak);
}

TEST_CASE("Ring Mod / Vocoder internal oscillator tracks the note with its offset") {
  ParamValues p = with({0.0f, 0.0f, 0.5f, 0.5f}, kCarrierOsc, kModInput, false);  // crossfade: all carrier
  p.p[6] = 0.75f;                                                                  // +12 st
  WarpsEngine engine;
  engine.prepare(kRate, 256);
  engine.noteOn(57.0f, 1.0f);  // A3 + 12 st = 440 Hz
  const std::vector<float> silent(96000, 0.0f);
  const Stereo out = run(engine, silent, silent, nullptr, nullptr, p);
  REQUIRE_THAT(test::estimateFrequency(out.l, kRate, 9600, 96000), Catch::Matchers::WithinRel(440.0, 0.003));
}

TEST_CASE("Ring Mod / Vocoder frequency shifter moves the voice by Warps' shift") {
  // Algorithm 0.9: 2|0.9 - 0.5| = 0.8 > 0.4, so 4 * 2^(180 * 0.4 / 12) = 256 Hz (modulator.cc:79-95).
  ParamValues p = with({0.9f, 0.98f, 0.0f, 1.0f}, kCarrierOsc, kModVoice, true);
  WarpsEngine engine;
  engine.prepare(kRate, 256);
  const auto voice = sine(96000, 1000.0, kRate);
  const Stereo out = run(engine, voice, voice, nullptr, nullptr, p);
  const double peak = test::peakFrequency(out.l, kRate, 9600, 96000, 200.0, 3000.0);
  INFO("peak " << peak);
  REQUIRE(std::fabs(std::fabs(peak - 1000.0) - 256.0) < 3.0);
}

TEST_CASE("Ring Mod / Vocoder with an absent modulator is silent") {
  WarpsEngine engine;
  engine.prepare(kRate, 256);
  const auto voice = sine(9600, 440.0, kRate);
  const Stereo out = run(engine, voice, voice, nullptr, nullptr, with({0.25f, 0.5f, 0.47f, 0.47f}, kCarrierVoice, kModInput, false));
  REQUIRE(test::rms(out.l, 0, out.l.size()) < 1e-4);
}

TEST_CASE("Ring Mod / Vocoder smooths its pots like the firmware") {
  WarpsEngine engine;
  engine.prepare(kRate, 256);
  const std::vector<float> block(kN, 0.1f);
  ParamValues p = defaults();
  p.p[0] = 0.25f;
  run(engine, block, block, nullptr, nullptr, p);
  REQUIRE(engine.smoothedParam(0) == 0.25f);  // snapped after reset
  p.p[0] = 0.75f;
  p.p[5] = kModInput;  // stepped: immediate
  p.p[6] = 1.0f;       // Osc Pitch: the note pot's 0.1
  run(engine, block, block, nullptr, nullptr, p);
  REQUIRE_THAT(engine.smoothedParam(0), Catch::Matchers::WithinAbs(0.25f + 0.0264f * 0.5f, 1e-6));
  REQUIRE(engine.smoothedParam(5) == kModInput);
  REQUIRE_THAT(engine.smoothedParam(6), Catch::Matchers::WithinAbs(0.5f + 0.1f * 0.5f, 1e-6));
  for (int i = 0; i < 400; ++i) run(engine, block, block, nullptr, nullptr, p);
  REQUIRE_THAT(engine.smoothedParam(0), Catch::Matchers::WithinAbs(0.75f, 1e-4));
  engine.reset();
  p.p[0] = 0.1f;
  run(engine, block, block, nullptr, nullptr, p);
  REQUIRE(engine.smoothedParam(0) == 0.1f);
}

TEST_CASE("Ring Mod / Vocoder runs its second modulator only for a stereo voice, fading it in") {
  WarpsEngine engine;
  engine.prepare(kRate, 256);
  const auto l = sine(kN * 40, 440.0, kRate);
  const auto r = sine(kN * 40, 660.0, kRate);
  const auto m = sine(kN * 40, 110.0, kRate);
  const ParamValues p = with({0.375f, 0.0f, 0.6f, 0.6f}, kCarrierVoice, kModInput, false);
  const Stereo mono = run(engine, l, r, nullptr, &m, p);
  REQUIRE_FALSE(engine.rightRuns());
  REQUIRE(mono.l == mono.r);
  engine.setStereo(true);
  const Stereo stereo = run(engine, l, r, nullptr, &m, p);
  REQUIRE(engine.rightRuns());
  // The right output starts on the left output and fades to its own instance over 10 ms.
  for (size_t i = 0; i < size_t(kN); ++i) REQUIRE(stereo.r[i] == stereo.l[i]);  // still the old block
  REQUIRE(std::fabs(stereo.r[kN] - stereo.l[kN]) < 0.01f);
  REQUIRE(stereo.r != stereo.l);
}

TEST_CASE("Ring Mod / Vocoder through the wrapper at 44.1, 48 and 96 kHz with odd host blocks") {
  for (double rate : {44100.0, 48000.0, 96000.0}) {
    NativeRateWrapper wrapped(std::make_unique<WarpsEngine>(), WarpsEngine::kNativeRate, WarpsEngine::kNativeBlock);
    wrapped.prepare(rate, 256);
    if (rate == kRate) REQUIRE(wrapped.inputLatencySamples() == kN);
    ParamValues p = with({0.0f, 0.0f, 0.5f, 0.5f}, kCarrierOsc, kModInput, false);
    p.p[6] = 0.75f;
    wrapped.noteOn(57.0f, 1.0f);
    const std::vector<float> silent(size_t(rate), 0.0f);
    const Stereo out = run(wrapped, silent, silent, nullptr, nullptr, p, 37);
    for (float s : out.l) REQUIRE(std::isfinite(s));
    INFO("rate " << rate);
    REQUIRE_THAT(test::estimateFrequency(out.l, rate, size_t(rate * 0.1), size_t(rate)),
                 Catch::Matchers::WithinRel(440.0, 0.003));
  }
}

TEST_CASE("Ring Mod / Vocoder output does not depend on the host block size at 96 kHz") {
  const auto voice = sine(kN * 50, 440.0, kRate);
  const auto side = sine(kN * 50, 97.0, kRate);
  const ParamValues p = with({0.6f, 0.5f, 0.7f, 0.7f}, kCarrierAux, kModInput, false);
  NativeRateWrapper a(std::make_unique<WarpsEngine>(), kRate, kN), b(std::make_unique<WarpsEngine>(), kRate, kN);
  a.prepare(kRate, 256);
  b.prepare(kRate, 256);
  REQUIRE(run(a, voice, voice, &side, &side, p, 60).l == run(b, voice, voice, &side, &side, p, 37).l);
}

TEST_CASE("Ring Mod / Vocoder survives NaN and every algorithm stays within Warps' range") {
  const auto voice = sine(kN * 40, 440.0, kRate, 2.0f);  // driven hard
  const auto side = sine(kN * 40, 97.0, kRate, 2.0f);
  const float limit = 32768.0f / WarpsEngine::kInputScale;  // Warps' 16-bit output, read back
  ParamValues bad;
  for (float& v : bad.p) v = std::numeric_limits<float>::quiet_NaN();
  {
    WarpsEngine engine;
    engine.prepare(kRate, 256);
    for (float s : run(engine, voice, voice, &side, &side, bad).l) REQUIRE(std::isfinite(s));
  }
  for (int step = 0; step <= 32; ++step) {
    for (float carrier : {kCarrierOsc, kCarrierVoice}) {
      for (bool shifter : {false, true}) {
        WarpsEngine engine;
        engine.prepare(kRate, 256);
        engine.setStereo(true);
        const Stereo out = run(engine, voice, side, nullptr, &side, with({float(step) / 32.0f, 0.7f, 1.0f, 1.0f}, carrier, kModInput, shifter));
        for (size_t i = 0; i < out.l.size(); ++i) {
          REQUIRE(std::fabs(out.l[i]) <= limit);
          REQUIRE(std::fabs(out.r[i]) <= limit);
        }
      }
    }
  }
}

TEST_CASE("Ring Mod / Vocoder processing does not allocate") {
  WarpsEngine engine;
  engine.prepare(kRate, 256);
  std::vector<float> l(kN, 0.2f), r(kN, -0.2f), m(kN, 0.1f), ol(kN), orr(kN);
  const float* in[4] = {l.data(), r.data(), m.data(), m.data()};
  float* out[2] = {ol.data(), orr.data()};
  ParamValues p = defaults();
  engine.process(in, out, p, kN);
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int block = 0; block < 400; ++block) {
      engine.setStereo(block % 100 < 50);
      p.p[0] = float(block % 9) / 8.0f;
      p.p[4] = WarpsEngine::optionValue(block % 5);
      p.p[5] = WarpsEngine::optionValue(block % 4);
      p.p[7] = float(block % 3) / 3.0f + 0.1f;
      p.p[8] = block % 7 == 0 ? 0.75f : 0.25f;
      engine.process(in, out, p, block % 2 ? kN : 17);
    }
    engine.reset();
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}

namespace {

// First sample whose magnitude reaches `fraction` of the largest magnitude, from `begin` on.
size_t onsetIndex(const std::vector<float>& x, size_t begin, float fraction) {
  float peak = 0.0f;
  for (size_t i = begin; i < x.size(); ++i) peak = std::max(peak, std::fabs(x[i]));
  for (size_t i = begin; i < x.size(); ++i) {
    if (std::fabs(x[i]) >= fraction * peak) return i;
  }
  return x.size();
}

// The onset of `x` in seconds after the wrapper's reported latency (`latency` host samples) has
// been taken off, interpolating the threshold crossing to a fraction of a sample.
double onsetAfterLatency(const std::vector<float>& x, size_t begin, double rate, int latency) {
  const float fraction = 0.1f;
  const size_t i = onsetIndex(x, begin, fraction);
  REQUIRE(i > begin);
  REQUIRE(i < x.size());
  float peak = 0.0f;
  for (size_t j = begin; j < x.size(); ++j) peak = std::max(peak, std::fabs(x[j]));
  const double level = double(fraction * peak);
  const double a = std::fabs(double(x[i - 1])), b = std::fabs(double(x[i]));
  const double crossing = double(i - 1) + (b > a ? (level - a) / (b - a) : 1.0);
  return (crossing - double(begin) - double(latency)) / rate;
}

}  // namespace

// Task 2 carry-forward: a converted-rate engine with its own latency (Warps' 60-sample FIFO at
// 96 kHz) starts where the wrapper says. What is left after the reported latency is Warps' own
// signal path (its oversampling filters, the oscillator's first cycle), which does not depend on
// the host rate, so it must be the same time at 44.1 and 48 kHz as at 96 kHz, where the wrapper
// is bypassed and the reported latency is exactly the FIFO. Tolerance: 2 host samples (rounding
// of the reported latency to whole samples and the resamplers' fractional delay).
TEST_CASE("Ring Mod / Vocoder output onset after reset lands at the wrapper's latencySamples()") {
  const auto q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  ParamValues p = with({0.0f, 0.0f, 0.5f, 0.5f}, kCarrierOsc, kModInput, false);  // the oscillator alone
  p.p[6] = 0.75f;
  double reference96 = 0.0;
  for (double rate : {96000.0, 44100.0, 48000.0}) {
    NativeRateWrapper wrapped(std::make_unique<WarpsEngine>(), kRate, kN);
    wrapped.setQuality(q);
    wrapped.prepare(rate, 256);
    if (rate == kRate) REQUIRE(wrapped.latencySamples() == kN);
    wrapped.noteOn(81.0f, 1.0f);  // 1760 Hz: a steep first rise
    wrapped.reset();
    const std::vector<float> silent(size_t(rate * 0.05), 0.0f);
    const Stereo out = run(wrapped, silent, silent, nullptr, nullptr, p, 37);
    const double onset = onsetAfterLatency(out.l, 0, rate, wrapped.latencySamples());
    if (rate == kRate) reference96 = onset;
    INFO("rate " << rate << " latency " << wrapped.latencySamples() << " onset after latency "
                 << onset * rate << " samples, at 96 kHz " << reference96 * rate);
    REQUIRE(onset >= 0.0);
    REQUIRE(std::fabs(onset - reference96) * rate <= 2.0);
  }
}

TEST_CASE("Ring Mod / Vocoder input onset lands at the wrapper's inputLatencySamples()") {
  const auto q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  const ParamValues p = with({0.0f, 0.0f, 0.5f, 0.5f}, kCarrierVoice, kModVoice, false);  // the voice alone
  double reference96 = 0.0;
  for (double rate : {96000.0, 44100.0, 48000.0}) {
    NativeRateWrapper wrapped(std::make_unique<WarpsEngine>(), kRate, kN);
    wrapped.setQuality(q);
    wrapped.prepare(rate, 256);
    if (rate == kRate) REQUIRE(wrapped.inputLatencySamples() == kN);
    const size_t start = size_t(rate * 0.02);
    std::vector<float> voice(size_t(rate * 0.08), 0.0f);
    for (size_t i = start; i < voice.size(); ++i) {
      voice[i] = 0.5f * float(std::sin(6.283185307179586 * 2000.0 * double(i - start) / rate));
    }
    const Stereo out = run(wrapped, voice, voice, nullptr, nullptr, p, 37);
    const double onset = onsetAfterLatency(out.l, start, rate, wrapped.inputLatencySamples());
    if (rate == kRate) reference96 = onset;
    INFO("rate " << rate << " input latency " << wrapped.inputLatencySamples() << " onset after latency "
                 << onset * rate << " samples, at 96 kHz " << reference96 * rate);
    REQUIRE(onset >= 0.0);
    REQUIRE(std::fabs(onset - reference96) * rate <= 2.0);
    REQUIRE(wrapped.inputUnderruns() == 0);
  }
}

// Upstream reads past its lookup tables at some exact knob values (see kMaxTablePosition).
// Under AddressSanitizer this sweeps them; in any build it pins the adapter's guard. Shifter
// cases use an external carrier: the internal oscillator's own wrap is not reachable from here.
TEST_CASE("Ring Mod / Vocoder keeps Warps' table reads in range at full Timbre and phase shift") {
  REQUIRE(WarpsEngine::tablePosition(1.0f) < 1.0f);
  REQUIRE(WarpsEngine::tablePosition(1.0f) * 256.0f < 256.0f);
  REQUIRE(WarpsEngine::tablePosition(0.3f) == 0.3f);
  REQUIRE(WarpsEngine::tablePosition(std::numeric_limits<float>::quiet_NaN()) == 0.0f);
  const auto voice = sine(kN * 20, 440.0, kRate);
  const auto side = sine(kN * 20, 97.0, kRate);
  for (float algorithm : {0.0f, 0.125f, 0.5f, 0.875f, 1.0f}) {
    for (float timbre : {0.0f, 0.5f, 1.0f}) {
      for (bool shifter : {false, true}) {
        WarpsEngine engine;
        engine.prepare(kRate, 256);
        const Stereo out = run(engine, voice, voice, nullptr, &side,
                               with({algorithm, timbre, 0.5f, 0.5f}, kCarrierVoice, kModInput, shifter));
        for (float s : out.l) REQUIRE(std::isfinite(s));
      }
    }
  }
}
