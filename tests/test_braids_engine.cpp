#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "alloc_counter.h"
#include "engine/braids_engine.h"
#include "engine/native_rate_wrapper.h"
#include "test_helpers.h"

using namespace eg13;

namespace {

std::vector<float> render(VoiceEngine& engine, const ParamValues& p, int n, int chunk = 256) {
  std::vector<float> left(static_cast<size_t>(n)), right(static_cast<size_t>(n));
  for (int done = 0; done < n;) {
    const int count = std::min(chunk, n - done);
    float* out[2] = {left.data() + done, right.data() + done};
    engine.process(nullptr, out, p, count);
    done += count;
  }
  return left;
}

ParamValues defaults() { return test::defaultParams(BraidsEngine::staticInfo()); }

}  // namespace

TEST_CASE("Digital Oscillator describes itself") {
  const auto& info = BraidsEngine::staticInfo();
  REQUIRE(std::string(info.id) == "braids");
  REQUIRE(std::string(info.displayName) == "Digital Oscillator");
  REQUIRE(info.paramCount == 7);
  REQUIRE_FALSE(info.usesInput);
  REQUIRE(std::string(info.params[0].name) == "Shape");
  char buf[64];
  info.params[0].format(info.params[0].defaultValue, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "CSAW");
  info.params[5].format(info.params[5].defaultValue, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "16 bit");
  info.params[6].format(info.params[6].defaultValue, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "96 kHz");
  REQUIRE(BraidsEngine::shapeIndex(1.0f) == 46);
  REQUIRE(BraidsEngine::shapeIndex(std::numeric_limits<float>::quiet_NaN()) == 0);
}

TEST_CASE("Digital Oscillator stays in tune through the wrapper") {
  for (double rate : {44100.0, 48000.0, 96000.0}) {
    NativeRateWrapper wrapped(std::make_unique<BraidsEngine>(), BraidsEngine::kNativeRate,
                              BraidsEngine::kNativeBlock);
    wrapped.prepare(rate, 512);
    wrapped.noteOn(69.0f, 1.0f);
    const int n = int(rate * 1.5);
    const auto out = render(wrapped, defaults(), n, 512);
    const double f = test::estimateFrequency(out, rate, size_t(rate * 0.5), size_t(n));
    INFO("rate " << rate << " measured " << f);
    REQUIRE_THAT(f, Catch::Matchers::WithinRel(440.0, 0.001));
  }
}

TEST_CASE("Digital Oscillator goes silent after note-off") {
  BraidsEngine engine;
  engine.prepare(BraidsEngine::kNativeRate, 512);
  engine.noteOn(60.0f, 1.0f);
  REQUIRE(test::rms(render(engine, defaults(), 9600), 0, 9600) > 0.05);
  engine.noteOff();
  const auto tail = render(engine, defaults(), 9600);
  REQUIRE(test::rms(tail, 4800, 9600) < 1e-4);
}

TEST_CASE("Velocity scales the Digital Oscillator") {
  BraidsEngine loud, soft;
  for (auto* e : {&loud, &soft}) e->prepare(BraidsEngine::kNativeRate, 512);
  loud.noteOn(60.0f, 1.0f);
  soft.noteOn(60.0f, 0.25f);
  const double a = test::rms(render(loud, defaults(), 9600), 4800, 9600);
  const double b = test::rms(render(soft, defaults(), 9600), 4800, 9600);
  REQUIRE_THAT(b / a, Catch::Matchers::WithinRel(0.25, 0.02));
}

TEST_CASE("Percussive shapes are struck on every note-on") {
  ParamValues p = defaults();
  p.p[0] = (34.0f + 0.5f) / 47.0f;  // Kick
  BraidsEngine engine;
  engine.prepare(BraidsEngine::kNativeRate, 512);
  engine.noteOn(40.0f, 1.0f);
  REQUIRE(test::rms(render(engine, p, 4800), 0, 4800) > 0.01);
  render(engine, p, 96000);  // decays
  engine.noteOn(40.0f, 1.0f);
  REQUIRE(test::rms(render(engine, p, 4800), 0, 4800) > 0.01);
}

TEST_CASE("Bit depth and sample-rate reduction change the sound") {
  BraidsEngine clean, crushed;
  for (auto* e : {&clean, &crushed}) {
    e->prepare(BraidsEngine::kNativeRate, 512);
    e->noteOn(60.0f, 1.0f);
  }
  ParamValues p = defaults();
  p.p[5] = 0.5f / 7.0f;  // 2 bit
  p.p[6] = 0.5f / 7.0f;  // 4 kHz
  REQUIRE(render(clean, defaults(), 4800) != render(crushed, p, 4800));
}

TEST_CASE("Digital Oscillator survives NaN parameters") {
  ParamValues bad;
  for (float& v : bad.p) v = std::numeric_limits<float>::quiet_NaN();
  BraidsEngine engine;
  engine.prepare(BraidsEngine::kNativeRate, 512);
  engine.noteOn(60.0f, 1.0f);
  for (float s : render(engine, bad, 4800)) REQUIRE(std::isfinite(s));
}

TEST_CASE("Digital Oscillator processing does not allocate") {
  BraidsEngine engine;
  engine.prepare(BraidsEngine::kNativeRate, 512);
  std::vector<float> l(512), r(512);
  float* out[2] = {l.data(), r.data()};
  const ParamValues p = defaults();
  engine.noteOn(60.0f, 1.0f);
  engine.process(nullptr, out, p, 512);
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int block = 0; block < 100; ++block) {
      if (block == 50) engine.noteOn(67.0f, 0.5f);
      engine.process(nullptr, out, p, 512);
    }
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}
