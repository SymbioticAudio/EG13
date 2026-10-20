#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "alloc_counter.h"
#include "engine/elements_engine.h"
#include "engine/native_rate_wrapper.h"
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

ParamValues defaults() { return test::defaultParams(ElementsEngine::staticInfo()); }

}  // namespace

TEST_CASE("Modal Synth describes itself") {
  const auto& info = ElementsEngine::staticInfo();
  REQUIRE(std::string(info.id) == "elements");
  REQUIRE(std::string(info.displayName) == "Modal Synth");
  REQUIRE(info.paramCount == 8);
  REQUIRE(info.usesInput);
  char buf[64];
  info.params[0].format(info.params[0].defaultValue, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Modal");
  REQUIRE(ElementsEngine::modelIndex(1.0f) == 2);
}

TEST_CASE("Modal Synth stays in tune through the wrapper") {
  for (double rate : {44100.0, 48000.0}) {
    NativeRateWrapper wrapped(std::make_unique<ElementsEngine>(), ElementsEngine::kNativeRate,
                              ElementsEngine::kNativeBlock);
    wrapped.prepare(rate, 256);
    wrapped.noteOn(69.0f, 1.0f);
    const int n = int(rate * 1.0);
    const auto out = render(wrapped, defaults(), n);
    const double f = test::estimateFrequency(out.l, rate, size_t(rate * 0.2), size_t(n));
    INFO("rate " << rate << " measured " << f);
    REQUIRE_THAT(f, Catch::Matchers::WithinRel(440.0, 0.003));
  }
}

TEST_CASE("Modal Synth is stereo and decays after note-off") {
  ParamValues p = defaults();
  ElementsEngine engine;
  engine.prepare(ElementsEngine::kNativeRate, 256);
  engine.noteOn(60.0f, 1.0f);
  const auto held = render(engine, p, 16000);
  REQUIRE(test::rms(held.l, 0, 16000) > 0.005);
  REQUIRE(held.l != held.r);
  engine.noteOff();
  const auto tail = render(engine, p, 96000);
  REQUIRE(test::rms(tail.l, 80000, 96000) < 1e-3);
}

TEST_CASE("Input excites the Modal Synth through its strike input") {
  ParamValues p = defaults();
  p.p[3] = 0.0f;  // internal strike off
  ElementsEngine engine;
  engine.prepare(ElementsEngine::kNativeRate, 256);
  std::vector<float> burst(16000, 0.0f);
  uint32_t seed = 1;
  for (size_t i = 0; i < 800; ++i) {
    seed = seed * 1664525u + 1013904223u;
    burst[i] = float(int32_t(seed)) / 2147483648.0f;
  }
  const auto out = render(engine, p, 16000, &burst);
  REQUIRE(test::rms(out.l, 1600, 16000) > 0.001);
}

TEST_CASE("Modal Synth survives NaN parameters") {
  ParamValues bad;
  for (float& v : bad.p) v = std::numeric_limits<float>::quiet_NaN();
  ElementsEngine engine;
  engine.prepare(ElementsEngine::kNativeRate, 256);
  engine.noteOn(60.0f, 1.0f);
  const auto out = render(engine, bad, 3200);
  for (float s : out.l) REQUIRE(std::isfinite(s));
}

TEST_CASE("Modal Synth processing does not allocate") {
  ElementsEngine engine;
  engine.prepare(ElementsEngine::kNativeRate, 256);
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
      if (block == 50) p.p[0] = 0.9f;  // model change
      engine.process(block % 2 ? input : nullptr, out, p, 256);
    }
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}
