#include <catch2/catch_test_macros.hpp>
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
#include "engine/rings_engine.h"
#include "rings/dsp/part.h"
#include "rings/dsp/patch.h"
#include "rings/dsp/performance_state.h"
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
  REQUIRE(info.paramCount == 8);
  REQUIRE(info.usesInput);
  char buf[64];
  info.params[0].format(info.params[0].defaultValue, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Modal");
  info.params[5].format(info.params[5].defaultValue, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Internal");
  REQUIRE(RingsEngine::modelIndex(1.0f) == 5);
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
