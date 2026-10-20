#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "alloc_counter.h"
#include "engine/native_rate_wrapper.h"
#include "engine/rings_engine.h"
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

TEST_CASE("Input excites the Resonator when Exciter is Input") {
  ParamValues p = defaults();
  p.p[5] = 0.75f;  // Input
  RingsEngine engine;
  engine.prepare(RingsEngine::kNativeRate, 256);
  std::vector<float> burst(9600, 0.0f);
  uint32_t seed = 7;
  for (size_t i = 0; i < 480; ++i) {
    seed = seed * 1664525u + 1013904223u;
    burst[i] = float(int32_t(seed)) / 2147483648.0f;
  }
  const auto out = render(engine, p, 9600, &burst);
  REQUIRE(test::rms(out.l, 960, 9600) > 0.0005);
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
