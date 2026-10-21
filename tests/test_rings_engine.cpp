#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "alloc_counter.h"
#include "engine/engine_registry.h"
#include "engine/native_rate_wrapper.h"
#include "engine/rings_engine.h"
#include "params/param_ids.h"
#include "rings/dsp/part.h"
#include "rings/dsp/patch.h"
#include "rings/dsp/performance_state.h"
#include "stmlib/utils/random.h"
#include "synth/param_table.h"
#include "synth/synth.h"
#include "test_helpers.h"

using namespace eg13;

namespace {

struct Stereo {
  std::vector<float> l, r;
};

Stereo render(VoiceEngine& engine, const ParamValues& p, int n, const std::vector<float>* input = nullptr) {
  Stereo s{std::vector<float>(static_cast<size_t>(n)), std::vector<float>(static_cast<size_t>(n))};
  for (int done = 0; done < n;) {
    const int count = std::min(256, n - done);
    float* out[2] = {s.l.data() + done, s.r.data() + done};
    if (input) {
      const float* in[2] = {input->data() + done, input->data() + done};
      engine.process(in, out, p, count);
    } else {
      engine.process(nullptr, out, p, count);
    }
    done += count;
  }
  return s;
}

ParamValues defaults() { return test::defaultParams(RingsEngine::staticInfo()); }

// P6 Exciter, stepped in thirds like the other stepped engine params: centres.
constexpr float kInternal = 0.5f / 3.0f;
constexpr float kInput = 1.5f / 3.0f;
constexpr float kBoth = 2.5f / 3.0f;

ParamValues withExciter(float exciter) {
  ParamValues p = defaults();
  p.p[5] = exciter;
  return p;
}

std::vector<float> noiseBurst() {
  std::vector<float> burst(9600, 0.0f);
  uint32_t seed = 7;
  for (size_t i = 0; i < 480; ++i) {
    seed = seed * 1664525u + 1013904223u;
    burst[i] = float(int32_t(seed)) / 2147483648.0f;
  }
  return burst;
}

}  // namespace

TEST_CASE("Resonator describes itself") {
  const auto& info = RingsEngine::staticInfo();
  REQUIRE(std::string(info.id) == "rings");
  REQUIRE(std::string(info.displayName) == "Resonator");
  REQUIRE(info.paramCount == 9);
  REQUIRE(info.usesInput);
  const char* const names[9] = {"Model",    "Structure", "Brightness", "Damping",  "Position",
                                "Exciter",  "Frequency", "Spread",     "Polyphony"};
  for (int i = 0; i < 9; ++i) REQUIRE(std::string(info.params[i].name) == names[i]);
  char buf[64];
  info.params[0].format(info.params[0].defaultValue, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Modal");
  info.params[5].format(info.params[5].defaultValue, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Internal");
  REQUIRE(RingsEngine::modelIndex(1.0f) == 5);
}

TEST_CASE("Polyphony is stepped 1 / 2 / 4, default 1") {
  REQUIRE(RingsEngine::polyphonyVoices(0.0f) == 1);
  REQUIRE(RingsEngine::polyphonyVoices(0.33f) == 1);
  REQUIRE(RingsEngine::polyphonyVoices(0.34f) == 2);
  REQUIRE(RingsEngine::polyphonyVoices(0.66f) == 2);
  REQUIRE(RingsEngine::polyphonyVoices(0.67f) == 4);
  REQUIRE(RingsEngine::polyphonyVoices(1.0f) == 4);
  REQUIRE(RingsEngine::polyphonyVoices(std::numeric_limits<float>::quiet_NaN()) == 1);
  const auto& param = RingsEngine::staticInfo().params[8];
  REQUIRE(param.defaultValue == 0.5f / 3.0f);
  char buf[64];
  const char* const shown[3] = {"1", "2", "4"};
  for (int i = 0; i < 3; ++i) {
    param.format((float(i) + 0.5f) / 3.0f, buf, sizeof(buf));
    REQUIRE(std::string(buf) == shown[i]);
  }
}

TEST_CASE("Resonator stays in tune through the wrapper") {
  for (double rate : {44100.0, 48000.0, 96000.0}) {
    NativeRateWrapper wrapped(std::make_unique<RingsEngine>(), RingsEngine::kNativeRate,
                              RingsEngine::kNativeBlock);
    wrapped.prepare(rate, 256);
    wrapped.noteOn(69.0f, 1.0f);
    const int n = int(rate * 1.0);
    const auto out = render(wrapped, defaults(), n);
    // Zero crossings are unreliable for the inharmonic resonator: use the spectral peak.
    const double f = test::peakFrequency(out.l, rate, size_t(rate * 0.1), size_t(rate * 0.6), 200.0, 2000.0);
    INFO("rate " << rate << " measured " << f);
    REQUIRE_THAT(f, Catch::Matchers::WithinRel(440.0, 0.003));
  }
}

TEST_CASE("Each note-on strums the Resonator") {
  RingsEngine engine;
  engine.prepare(RingsEngine::kNativeRate, 256);
  engine.noteOn(60.0f, 1.0f);
  REQUIRE(test::rms(render(engine, defaults(), 4800).l, 0, 4800) > 0.005);
  render(engine, defaults(), 480000);  // let it ring out
  const double quiet = test::rms(render(engine, defaults(), 4800).l, 0, 4800);
  engine.noteOn(60.0f, 1.0f);
  const double struck = test::rms(render(engine, defaults(), 4800).l, 0, 4800);
  REQUIRE(struck > 10.0 * quiet);
}

TEST_CASE("Spread 0 is mono and spread 1 separates odd and even") {
  ParamValues mono = defaults();
  mono.p[7] = 0.0f;
  RingsEngine a, b;
  for (auto* e : {&a, &b}) {
    e->prepare(RingsEngine::kNativeRate, 256);
    e->noteOn(60.0f, 1.0f);
  }
  const auto m = render(a, mono, 4800);
  REQUIRE(m.l == m.r);
  const auto s = render(b, defaults(), 4800);
  REQUIRE(s.l != s.r);
}

TEST_CASE("Exciter is stepped Internal / Input / Both") {
  REQUIRE(RingsEngine::exciterIndex(0.0f) == 0);
  REQUIRE(RingsEngine::exciterIndex(0.33f) == 0);
  REQUIRE(RingsEngine::exciterIndex(0.34f) == 1);
  REQUIRE(RingsEngine::exciterIndex(0.66f) == 1);
  REQUIRE(RingsEngine::exciterIndex(0.67f) == 2);
  REQUIRE(RingsEngine::exciterIndex(1.0f) == 2);
  REQUIRE(RingsEngine::exciterIndex(std::numeric_limits<float>::quiet_NaN()) == 0);
  const auto& param = RingsEngine::staticInfo().params[5];
  REQUIRE(param.defaultValue == kInternal);
  char buf[64];
  const char* const names[3] = {"Internal", "Input", "Both"};
  const float centres[3] = {kInternal, kInput, kBoth};
  for (int i = 0; i < 3; ++i) {
    REQUIRE(RingsEngine::exciterIndex(centres[i]) == i);
    param.format(centres[i], buf, sizeof(buf));
    REQUIRE(std::string(buf) == names[i]);
  }
}

namespace {

// Upstream Rings as the engine drives it at defaults (FM Voice, Exciter
// Internal, no input, native rate), one 24-sample block per call, strummed once.
struct UpstreamRings {
  rings::Part part;
  uint16_t reverb[32768];
};

void renderUpstream(int blocks, std::vector<float>& out, std::vector<float>& aux) {
  auto u = std::make_unique<UpstreamRings>();
  std::memset(static_cast<void*>(&u->part), 0, sizeof(u->part));
  u->part.Init(u->reverb);
  u->part.set_polyphony(1);
  u->part.set_model(rings::RESONATOR_MODEL_FM_VOICE);
  constexpr int kN = RingsEngine::kNativeBlock;
  float in[kN] = {};
  out.assign(size_t(blocks * kN), 0.0f);
  aux.assign(size_t(blocks * kN), 0.0f);
  for (int b = 0; b < blocks; ++b) {
    rings::Patch patch;
    patch.structure = 0.5f;
    patch.brightness = 0.5f;
    patch.damping = 0.5f;
    patch.position = 0.5f;
    rings::PerformanceState state;
    state.strum = b == 0;
    state.internal_exciter = true;
    state.internal_strum = false;
    state.internal_note = true;
    state.tonic = 60.0f;
    state.note = 0.0f;
    state.fm = 0.0f;
    state.chord = 5;
    u->part.Process(state, patch, in, out.data() + b * kN, aux.data() + b * kN, size_t(kN));
  }
}

}  // namespace

TEST_CASE("Spread folds odd and even the way the firmware does (out - aux)") {
  constexpr int kBlocks = 200;
  constexpr int kN = RingsEngine::kNativeBlock;
  std::vector<float> odd, even;
  renderUpstream(kBlocks, odd, even);
  for (float spread : {0.0f, 1.0f}) {
    ParamValues p = defaults();
    p.p[0] = 3.5f / 6.0f;  // FM Voice: out = carrier + mod/2, aux = mod/2
    p.p[7] = spread;
    RingsEngine engine;
    engine.prepare(RingsEngine::kNativeRate, 256);
    engine.noteOn(60.0f, 1.0f);
    std::vector<float> l(size_t(kBlocks * kN)), r(size_t(kBlocks * kN));
    for (int b = 0; b < kBlocks; ++b) {
      float* out[2] = {l.data() + b * kN, r.data() + b * kN};
      engine.process(nullptr, out, p, kN);
    }
    INFO("spread " << spread);
    REQUIRE(test::rms(odd, 0, odd.size()) > 0.01);
    REQUIRE(test::rms(even, 0, even.size()) > 0.001);
    for (size_t i = 0; i < l.size(); ++i) {
      if (spread == 0.0f) {  // mono: L = R = odd - even, the firmware's mono fold
        REQUIRE(l[i] == r[i]);
        REQUIRE(std::fabs(l[i] - (odd[i] - even[i])) <= 1e-6f);
      } else {  // L = odd, R = -even (same pickup convention; the polarity flip is inaudible)
        REQUIRE(l[i] == odd[i]);
        REQUIRE(r[i] == -even[i]);
      }
    }
  }
}

TEST_CASE("Input excites the Resonator when Exciter is Input") {
  RingsEngine engine;
  engine.prepare(RingsEngine::kNativeRate, 256);
  const auto burst = noiseBurst();
  const auto out = render(engine, withExciter(kInput), 9600, &burst);
  REQUIRE(test::rms(out.l, 960, 9600) > 0.0005);
}

TEST_CASE("Exciter Internal ignores the input") {
  RingsEngine engine;
  engine.prepare(RingsEngine::kNativeRate, 256);
  const auto burst = noiseBurst();
  const auto out = render(engine, withExciter(kInternal), 9600, &burst);
  REQUIRE(test::rms(out.l, 0, 9600) < 1e-6);
}

TEST_CASE("Exciter Both responds to a note-on strike and to the input") {
  const auto burst = noiseBurst();
  const std::vector<float> silence(9600, 0.0f);
  SECTION("input alone, no note") {
    RingsEngine engine;
    engine.prepare(RingsEngine::kNativeRate, 256);
    const auto out = render(engine, withExciter(kBoth), 9600, &burst);
    REQUIRE(test::rms(out.l, 960, 9600) > 0.0005);
  }
  SECTION("note-on strike with a silent input") {
    RingsEngine engine;
    engine.prepare(RingsEngine::kNativeRate, 256);
    engine.noteOn(60.0f, 1.0f);
    const auto out = render(engine, withExciter(kBoth), 4800, &silence);
    REQUIRE(test::rms(out.l, 0, 4800) > 0.005);
  }
  SECTION("strike plus input is louder than the strike alone") {
    RingsEngine internal, both;
    for (auto* e : {&internal, &both}) {
      e->prepare(RingsEngine::kNativeRate, 256);
      e->noteOn(60.0f, 1.0f);
    }
    const double a = test::rms(render(internal, withExciter(kInternal), 9600, &burst).l, 0, 9600);
    const double b = test::rms(render(both, withExciter(kBoth), 9600, &burst).l, 0, 9600);
    INFO("internal " << a << " both " << b);
    REQUIRE(b > 1.1 * a);
  }
}

TEST_CASE("Resonator survives NaN parameters") {
  ParamValues bad;
  for (float& v : bad.p) v = std::numeric_limits<float>::quiet_NaN();
  RingsEngine engine;
  engine.prepare(RingsEngine::kNativeRate, 256);
  engine.noteOn(60.0f, 1.0f);
  const auto out = render(engine, bad, 4800);
  for (float s : out.l) REQUIRE(std::isfinite(s));
}

TEST_CASE("Resonator processing does not allocate") {
  RingsEngine engine;
  engine.prepare(RingsEngine::kNativeRate, 256);
  std::vector<float> in(256, 0.1f), l(256), r(256);
  const float* input[2] = {in.data(), in.data()};
  float* out[2] = {l.data(), r.data()};
  ParamValues p = defaults();
  engine.noteOn(60.0f, 1.0f);
  engine.process(input, out, p, 256);
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int block = 0; block < 100; ++block) {
      if (block == 30) p.p[0] = 0.4f;  // model change
      if (block == 60) engine.noteOn(64.0f, 0.7f);
      engine.process(block % 2 ? input : nullptr, out, p, 256);
    }
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}

namespace {

constexpr float kPoly1 = 0.5f / 3.0f;
constexpr float kPoly2 = 1.5f / 3.0f;
constexpr float kPoly4 = 2.5f / 3.0f;

// Hann-windowed Goertzel magnitude of x[begin, end) at f Hz.
double magnitudeAt(const std::vector<float>& x, double rate, size_t begin, size_t end, double f) {
  const size_t count = end - begin;
  const double pi = 3.14159265358979323846;
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

double noteHz(float note) { return 440.0 * std::pow(2.0, (double(note) - 69.0) / 12.0); }

// How far f stands out of its neighbourhood: magnitude at f over the mean
// magnitude a quarter tone either side.
double peakness(const std::vector<float>& x, double rate, size_t begin, size_t end, double f) {
  const double q = std::pow(2.0, 1.0 / 24.0);
  const double side = 0.5 * (magnitudeAt(x, rate, begin, end, f / q) +
                             magnitudeAt(x, rate, begin, end, f * q));
  return magnitudeAt(x, rate, begin, end, f) / std::max(side, 1e-12);
}

constexpr float kStrumNotes[4] = {57.0f, 62.0f, 66.0f, 71.0f};
constexpr int kStrumGap = 12000;  // 0.25 s at the native rate

// Four strums of different notes with no note-off, mono (Spread 0). Returns L;
// the fourth strum lands at 3 * kStrumGap.
std::vector<float> renderFourStrums(float polyphony, int tail) {
  ParamValues p = defaults();
  p.p[7] = 0.0f;
  p.p[8] = polyphony;
  RingsEngine engine;
  engine.prepare(RingsEngine::kNativeRate, 256);
  std::vector<float> l;
  for (float note : kStrumNotes) {
    engine.noteOn(note, 1.0f);
    const auto s = render(engine, p, note == kStrumNotes[3] ? tail : kStrumGap);
    l.insert(l.end(), s.l.begin(), s.l.end());
  }
  return l;
}

}  // namespace

TEST_CASE("Polyphony 4: four strums of different notes all keep sounding") {
  const double rate = RingsEngine::kNativeRate;
  const auto poly4 = renderFourStrums(kPoly4, 24000);
  const auto poly1 = renderFourStrums(kPoly1, 24000);
  const size_t begin = 3 * kStrumGap + 2400, end = begin + 19200;  // 0.05 s .. 0.45 s after
  const double latest = magnitudeAt(poly4, rate, begin, end, noteHz(kStrumNotes[3]));
  for (int k = 0; k < 4; ++k) {
    const double f = noteHz(kStrumNotes[k]);
    const double m4 = magnitudeAt(poly4, rate, begin, end, f);
    const double m1 = magnitudeAt(poly1, rate, begin, end, f);
    const double peak = peakness(poly4, rate, begin, end, f);
    INFO("strum " << k + 1 << " at " << f << " Hz: poly4 " << m4 << " (peakness " << peak
                  << "), poly1 " << m1 << ", latest strum " << latest);
    REQUIRE(peak > 4.0);             // a spectral peak at each strummed note
    REQUIRE(m4 > 0.05 * latest);     // the earlier strums still sound against the latest one
    if (k < 3) REQUIRE(m4 > 10.0 * m1);  // and far more than at polyphony 1
  }
}

TEST_CASE("Polyphony 1: a new strum at another note silences the earlier one") {
  const double rate = RingsEngine::kNativeRate;
  const auto poly1 = renderFourStrums(kPoly1, 24000);
  const size_t begin = 3 * kStrumGap + 2400, end = begin + 19200;
  const double latest = magnitudeAt(poly1, rate, begin, end, noteHz(kStrumNotes[3]));
  REQUIRE(peakness(poly1, rate, begin, end, noteHz(kStrumNotes[3])) > 4.0);
  for (int k = 0; k < 3; ++k) {
    const double f = noteHz(kStrumNotes[k]);
    const double m = magnitudeAt(poly1, rate, begin, end, f);
    INFO("strum " << k + 1 << " at " << f << " Hz: " << m << ", latest strum " << latest);
    REQUIRE(m < 0.01 * latest);
  }
}

namespace {

// Upstream Rings as the engine drives it (defaults, Exciter Internal, no input,
// native rate, 24-sample blocks) with the given polyphony and model, strummed
// at each block in strumBlocks with the matching note. The engine passes the
// played note as performance_state.note relative to a C4 tonic, as the
// firmware does with V/OCT patched (rings/cv_scaler.cc:193-197).
void renderUpstreamPoly(int polyphony, rings::ResonatorModel model, const std::vector<int>& strumBlocks,
                        const std::vector<float>& notes, int blocks, std::vector<float>& out,
                        std::vector<float>& aux) {
  auto u = std::make_unique<UpstreamRings>();
  std::memset(static_cast<void*>(&u->part), 0, sizeof(u->part));
  u->part.Init(u->reverb);
  u->part.set_polyphony(1);
  u->part.set_model(model);
  u->part.set_polyphony(polyphony);
  constexpr int kN = RingsEngine::kNativeBlock;
  float in[kN] = {};
  out.assign(size_t(blocks * kN), 0.0f);
  aux.assign(size_t(blocks * kN), 0.0f);
  float note = notes[0];
  for (int b = 0; b < blocks; ++b) {
    bool strum = false;
    for (size_t s = 0; s < strumBlocks.size(); ++s) {
      if (strumBlocks[s] == b) {
        strum = true;
        note = notes[s];
      }
    }
    rings::Patch patch;
    patch.structure = 0.5f;
    patch.brightness = 0.5f;
    patch.damping = 0.5f;
    patch.position = 0.5f;
    rings::PerformanceState state;
    state.strum = strum;
    state.internal_exciter = true;
    state.internal_strum = false;
    state.internal_note = false;
    state.tonic = 60.0f;
    state.note = note - 60.0f;
    state.fm = 0.0f;
    state.chord = 5;
    u->part.Process(state, patch, in, out.data() + b * kN, aux.data() + b * kN, size_t(kN));
  }
}

}  // namespace

// part.cc:530-543: at polyphony 1 out/aux carry one voice's odd/even pickups
// (folded out - aux); above 1 each voice is already folded (out - aux) and
// whole voices go to out (0, 2) and aux (1, 3), so the mono sum is out + aux.
// String + Reverb negates aux after the reverb (part.cc:559-561) at any
// polyphony, so there the fold stays out - aux.
TEST_CASE("Spread folds whole voices the way the firmware dispatches them above polyphony 1") {
  constexpr int kBlocks = 600;
  constexpr int kN = RingsEngine::kNativeBlock;
  const std::vector<int> strumBlocks = {0, 150, 300, 450};
  const std::vector<float> notes = {57.0f, 62.0f, 66.0f, 71.0f};
  struct Case {
    int voices;
    float polyParam;
    int model;
  };
  for (const Case c : {Case{2, kPoly2, 3}, Case{4, kPoly4, 3}, Case{4, kPoly4, 2}, Case{2, kPoly2, 5}}) {
    std::vector<float> odd, even;
    stmlib::Random::Seed(1);  // the String pluck draws noise; both renders draw the same
    renderUpstreamPoly(c.voices, rings::ResonatorModel(c.model), strumBlocks, notes, kBlocks, odd, even);
    const float evenSign = c.model == 5 ? -1.0f : 1.0f;
    for (float spread : {0.0f, 1.0f}) {
      ParamValues p = defaults();
      p.p[0] = (float(c.model) + 0.5f) / 6.0f;
      p.p[7] = spread;
      p.p[8] = c.polyParam;
      RingsEngine engine;
      engine.prepare(RingsEngine::kNativeRate, 256);
      stmlib::Random::Seed(1);
      std::vector<float> l(size_t(kBlocks * kN)), r(size_t(kBlocks * kN));
      size_t next = 0;
      for (int b = 0; b < kBlocks; ++b) {
        if (next < strumBlocks.size() && strumBlocks[next] == b) engine.noteOn(notes[next++], 1.0f);
        float* out[2] = {l.data() + b * kN, r.data() + b * kN};
        engine.process(nullptr, out, p, kN);
      }
      INFO("voices " << c.voices << " model " << c.model << " spread " << spread);
      REQUIRE(test::rms(odd, 0, odd.size()) > 0.001);
      REQUIRE(test::rms(even, 0, even.size()) > 0.001);
      for (size_t i = 0; i < l.size(); ++i) {
        if (spread == 0.0f) {
          REQUIRE(l[i] == r[i]);
          REQUIRE(std::fabs(l[i] - (odd[i] + evenSign * even[i])) <= 1e-6f);
        } else {
          REQUIRE(l[i] == odd[i]);
          REQUIRE(r[i] == evenSign * even[i]);
        }
      }
    }
  }
}

TEST_CASE("Changing polyphony does not allocate") {
  RingsEngine engine;
  engine.prepare(RingsEngine::kNativeRate, 256);
  std::vector<float> l(256), r(256);
  float* out[2] = {l.data(), r.data()};
  ParamValues p = defaults();
  engine.noteOn(60.0f, 1.0f);
  engine.process(nullptr, out, p, 256);
  const float settings[4] = {kPoly4, kPoly2, kPoly1, kPoly4};
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int block = 0; block < 100; ++block) {
      if (block % 25 == 0) p.p[8] = settings[block / 25];
      if (block % 10 == 5) engine.noteOn(55.0f + float(block % 7), 0.8f);
      if (block == 50) p.p[0] = 0.4f;  // model change at polyphony 1
      engine.process(nullptr, out, p, 256);
    }
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}

// For information: the budget is judged at polyphony 1 by "CPU: 16 voices per
// engine stay within budget". Same load, Resonator only, at each polyphony.
TEST_CASE("CPU: Resonator at polyphony 1, 2 and 4", "[.benchmark]") {
  constexpr double kRate = 48000.0;
  constexpr int kBlock = 128;
  constexpr int kSeconds = 5;
  for (const float polyphony : {kPoly1, kPoly2, kPoly4}) {
    Synth synth(PluginKind::Instrument);
    synth.setParam(paramSpec(masterParamIndex(MasterParam::Mode)).id, double(PartMode::Layer));
    for (int part = 0; part < kNumParts; ++part) {
      synth.setParam(paramSpec(sourceEngineParamIndex(part)).id, double(SourceEngine::Resonator));
      synth.setParam(paramSpec(sourceParamIndex(part, 8)).id, double(polyphony));
    }
    for (int part = 0; part < kNumParts; ++part) {
      REQUIRE(synth.param(paramSpec(sourceParamIndex(part, 8)).id) == double(polyphony));
    }
    synth.prepare(kRate, kBlock);
    synth.setActive(true);
    std::vector<float> in(kBlock, 0.1f), l(kBlock), r(kBlock);
    const float* input[2] = {in.data(), in.data()};
    float* out[2] = {l.data(), r.data()};
    synth.process(input, out, nullptr, kBlock);  // warm up
    for (int key = 48; key < 56; ++key) synth.noteOn(0, key, 0.8f);
    const int blocks = int(kRate) * kSeconds / kBlock;
    const auto start = std::chrono::steady_clock::now();
    for (int b = 0; b < blocks; ++b) {
      if (b % 375 == 0) {
        for (int key = 48; key < 56; ++key) synth.noteOn(0, key, 0.8f);
      }
      synth.process(input, out, nullptr, kBlock);
    }
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("Resonator polyphony %d %6.2f%% of one core (16 voices, 48 kHz, 128-sample blocks)\n",
                RingsEngine::polyphonyVoices(polyphony), 100.0 * seconds / double(kSeconds));
    synth.setActive(false);
  }
}

TEST_CASE("Resonator smooths its pots like the firmware") {
  // CvScaler::Read runs once per 24-sample block (rings.cc:96, dsp.h:41) and low-passes every
  // pot with adc_lp_ += 0.01 * (pot - adc_lp_) (cv_scaler.cc:53-57, 171): Frequency,
  // Structure, Brightness, Damping, Position.
  RingsEngine engine;
  engine.prepare(RingsEngine::kNativeRate, 256);
  std::vector<float> l(24), r(24);
  float* out[2] = {l.data(), r.data()};
  ParamValues p = defaults();
  p.p[2] = 0.0f;  // Brightness
  engine.process(nullptr, out, p, 24);
  REQUIRE(engine.smoothedParam(2) == 0.0f);  // first render after reset snaps
  REQUIRE(engine.smoothedParam(1) == 0.5f);

  ParamValues step = p;
  step.p[0] = 0.99f;  // Model: stepped
  step.p[1] = 1.0f;   // Structure 0.5 -> 1
  step.p[2] = 1.0f;   // Brightness 0 -> 1
  step.p[3] = 0.0f;   // Damping 0.5 -> 0
  step.p[4] = 1.0f;   // Position 0.5 -> 1
  step.p[5] = kBoth;  // Exciter: stepped
  step.p[6] = 1.0f;   // Frequency 0.5 -> 1
  step.p[7] = 0.0f;   // Spread: EG13-only: not pot-smoothed (de-clicked per sample)
  step.p[8] = 0.99f;  // Polyphony: stepped
  engine.process(nullptr, out, step, 24);
  REQUIRE(engine.smoothedParam(0) == 0.99f);
  REQUIRE_THAT(engine.smoothedParam(1), Catch::Matchers::WithinAbs(0.505, 1e-6));
  REQUIRE_THAT(engine.smoothedParam(2), Catch::Matchers::WithinAbs(0.01, 1e-6));
  REQUIRE_THAT(engine.smoothedParam(3), Catch::Matchers::WithinAbs(0.495, 1e-6));
  REQUIRE_THAT(engine.smoothedParam(4), Catch::Matchers::WithinAbs(0.505, 1e-6));
  REQUIRE(engine.smoothedParam(5) == kBoth);
  REQUIRE_THAT(engine.smoothedParam(6), Catch::Matchers::WithinAbs(0.505, 1e-6));
  REQUIRE(engine.smoothedParam(7) == 0.0f);
  REQUIRE(engine.smoothedParam(8) == 0.99f);

  // Gradual: no block moves Brightness more than one 0.01 pole can; 0.99^n remains.
  float previous = engine.smoothedParam(2);
  for (int block = 2; block <= 2000; ++block) {
    engine.process(nullptr, out, step, 24);
    const float v = engine.smoothedParam(2);
    INFO("block " << block << " value " << v);
    REQUIRE(v >= previous);
    REQUIRE(v - previous <= 0.01f);
    if (block == 100) {
      REQUIRE_THAT(v, Catch::Matchers::WithinAbs(1.0 - std::pow(0.99, 100), 1e-4));
    }
    previous = v;
  }
  REQUIRE(previous > 0.9999f);

  // A partial block advances the filter by its share of a native block.
  RingsEngine c;
  c.prepare(RingsEngine::kNativeRate, 256);
  c.process(nullptr, out, p, 24);
  for (int i = 0; i < 3; ++i) c.process(nullptr, out, step, 8);
  REQUIRE_THAT(c.smoothedParam(2), Catch::Matchers::WithinAbs(0.01, 1e-6));

  // reset() snaps again on the next render.
  engine.reset();
  engine.process(nullptr, out, step, 24);
  REQUIRE(engine.smoothedParam(2) == 1.0f);
  REQUIRE(engine.smoothedParam(3) == 0.0f);
}

TEST_CASE("Resonator smoothing keeps its time constant at 16-sample calls") {
  RingsEngine e;
  e.prepare(RingsEngine::kNativeRate, 256);
  std::vector<float> l(16), r(16);
  float* out[2] = {l.data(), r.data()};
  ParamValues p = defaults();
  p.p[2] = 0.0f;
  e.process(nullptr, out, p, 16);
  p.p[2] = 1.0f;
  for (int i = 0; i < 150; ++i) e.process(nullptr, out, p, 16);  // 2400 samples = 100 native blocks
  REQUIRE_THAT(e.smoothedParam(2), Catch::Matchers::WithinAbs(1.0 - std::pow(0.99, 100), 1e-4));
}

TEST_CASE("Resonator smoothing is independent of host block size") {
  // One call of several native blocks renders exactly what native-block calls render.
  ParamValues p = defaults();
  ParamValues step = p;
  step.p[1] = 0.9f;
  step.p[2] = 0.1f;
  step.p[3] = 0.8f;
  step.p[6] = 0.7f;
  RingsEngine a, b;
  for (auto* e : {&a, &b}) {
    e->prepare(RingsEngine::kNativeRate, 4800);
    e->noteOn(57.0f, 1.0f);
  }
  std::vector<float> al(4800), ar(4800), bl(4800), br(4800);
  float* outA[2] = {al.data(), ar.data()};
  a.process(nullptr, outA, p, 240);
  float* outA2[2] = {al.data() + 240, ar.data() + 240};
  a.process(nullptr, outA2, step, 4560);
  for (int done = 0; done < 4800; done += 24) {
    float* outB[2] = {bl.data() + done, br.data() + done};
    b.process(nullptr, outB, done < 240 ? p : step, 24);
  }
  REQUIRE(al == bl);
  REQUIRE(ar == br);
  REQUIRE(a.smoothedParam(2) == b.smoothedParam(2));
}

TEST_CASE("Resonator processing with moving pots does not allocate") {
  RingsEngine engine;
  engine.prepare(RingsEngine::kNativeRate, 256);
  std::vector<float> l(256), r(256);
  float* out[2] = {l.data(), r.data()};
  ParamValues p = defaults();
  engine.noteOn(60.0f, 1.0f);
  engine.process(nullptr, out, p, 256);
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int block = 0; block < 100; ++block) {
      p.p[1 + block % 4] = float(block % 7) / 6.0f;
      p.p[6] = float(block % 5) / 4.0f;
      p.p[7] = float(block % 3) / 2.0f;  // Spread
      if (block == 50) engine.reset();
      engine.process(nullptr, out, p, 256);
    }
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}

TEST_CASE("Resonator smooths Spread per sample") {
  // Spread is EG13-only, so it gets a ~5 ms per-sample de-click one-pole.
  const double rate = RingsEngine::kNativeRate;
  const float c = float(1.0 - std::exp(-1.0 / (0.005 * rate)));
  RingsEngine engine;
  engine.prepare(rate, 256);
  engine.noteOn(60.0f, 1.0f);
  ParamValues p = defaults();
  p.p[7] = 0.0f;
  render(engine, p, 240);
  REQUIRE(engine.smoothedSpread() == 0.0f);  // snapped
  ParamValues step = p;
  step.p[7] = 1.0f;
  std::vector<float> l(1), r(1);
  float* out[2] = {l.data(), r.data()};
  float previous = engine.smoothedSpread();
  for (int i = 1; i <= 4000; ++i) {
    engine.process(nullptr, out, step, 1);
    const float v = engine.smoothedSpread();
    INFO("sample " << i << " value " << v);
    REQUIRE(v >= previous);
    // One pole step, or the final snap once a step can no longer move the float near 1
    // (a stall at ~7e-6 from the target, about -100 dB).
    REQUIRE(v - previous <= (v == 1.0f ? 1e-5f : c * (1.0f - previous) + 1e-6f));
    if (i == 240) REQUIRE_THAT(v, Catch::Matchers::WithinAbs(1.0 - std::exp(-240.0 / (0.005 * rate)), 1e-4));
    previous = v;
  }
  REQUIRE(previous == 1.0f);  // converged (~17 time constants) and snapped within 1e-6
  engine.reset();
  engine.process(nullptr, out, step, 1);
  REQUIRE(engine.smoothedSpread() == 1.0f);
}

TEST_CASE("Resonator AUX is upstream Rings' even output") {
  REQUIRE(RingsEngine::staticInfo().hasAux);
  constexpr int kBlocks = 200;
  constexpr int kN = RingsEngine::kNativeBlock;
  std::vector<float> odd, even;
  renderUpstream(kBlocks, odd, even);
  ParamValues p = defaults();
  p.p[0] = 3.5f / 6.0f;  // FM Voice, as renderUpstream
  RingsEngine engine;
  engine.setAuxEnabled(true);
  engine.prepare(RingsEngine::kNativeRate, 256);
  engine.noteOn(60.0f, 1.0f);
  std::vector<float> l(size_t(kBlocks * kN)), r(l.size()), aux(l.size());
  for (int b = 0; b < kBlocks; ++b) {
    float* out[3] = {l.data() + b * kN, r.data() + b * kN, aux.data() + b * kN};
    engine.process(nullptr, out, p, kN);
  }
  REQUIRE(test::rms(even, 0, even.size()) > 0.001);
  for (size_t i = 0; i < aux.size(); ++i) REQUIRE(aux[i] == even[i]);
}
