#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
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
#include "voice/source_factory.h"
#include "voice/voice.h"

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
  const auto q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  for (double rate : {44100.0, 48000.0, 96000.0}) {
    NativeRateWrapper wrapped(std::make_unique<BraidsEngine>(), BraidsEngine::kNativeRate,
                              BraidsEngine::kNativeBlock);
    wrapped.setQuality(q);
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

TEST_CASE("Sample-rate reduction keeps its phase across short process calls") {
  // At a 96 kHz host the Voice feeds Braids 16-sample calls; the hold period
  // must stay regular, as if the whole render were one call. The upstream
  // oscillator's own output depends on the call size, so the reference is the
  // same 16-sample-call render without reduction, held on the global grid.
  ParamValues full = defaults();  // 96 kHz: no decimation
  BraidsEngine reference;
  reference.prepare(BraidsEngine::kNativeRate, 512);
  reference.noteOn(60.0f, 1.0f);
  const auto clean = render(reference, full, 9600, 16);
  const int decimation[7] = {24, 12, 6, 4, 3, 2, 1};
  for (int rate = 0; rate < 7; ++rate) {
    ParamValues p = defaults();
    p.p[6] = (float(rate) + 0.5f) / 7.0f;
    BraidsEngine engine;
    engine.prepare(BraidsEngine::kNativeRate, 512);
    engine.noteOn(60.0f, 1.0f);
    const auto held = render(engine, p, 9600, 16);
    INFO("sample rate index " << rate);
    const size_t d = size_t(decimation[rate]);
    for (size_t i = 4800; i < held.size(); ++i) REQUIRE(held[i] == clean[i - i % d]);  // gain settled
  }
}

TEST_CASE("Signature at max: a silent Digital Oscillator outputs 0") {
  ParamValues p = defaults();
  p.p[4] = 1.0f;  // Signature 100%
  BraidsEngine engine;
  engine.prepare(BraidsEngine::kNativeRate, 512);
  for (float s : render(engine, p, 4800)) REQUIRE(s == 0.0f);
}

TEST_CASE("Signature at max: a released voice decays to silence and frees") {
  ParamValues p = defaults();
  p.p[4] = 1.0f;
  Voice voice(makeSource(1));  // Digital Oscillator
  voice.prepare(48000.0, 256);
  voice.trigger(60, 1.0f);
  std::vector<float> l(256), r(256);
  float* out[2] = {l.data(), r.data()};
  for (int block = 0; block < 40; ++block) voice.process(out, p, 256);
  voice.release();
  std::vector<float> tail;
  for (int block = 0; block < 100 && voice.active(); ++block) {
    std::fill(l.begin(), l.end(), 0.0f);
    std::fill(r.begin(), r.end(), 0.0f);
    voice.process(out, p, 256);
    tail.insert(tail.end(), l.begin(), l.end());
  }
  REQUIRE_FALSE(voice.active());
  REQUIRE(tail.size() >= 2400);  // freed after the Voice's silence window
  REQUIRE(test::rms(tail, tail.size() - 2400, tail.size()) < 1e-4);
}

TEST_CASE("Signature drive follows the level, as in the firmware (VCA first)") {
  ParamValues p = defaults();
  p.p[4] = 1.0f;
  BraidsEngine loud, soft;
  for (auto* e : {&loud, &soft}) e->prepare(BraidsEngine::kNativeRate, 512);
  loud.noteOn(60.0f, 1.0f);
  soft.noteOn(60.0f, 0.25f);
  auto a = render(loud, p, 9600);
  auto b = render(soft, p, 9600);
  auto normalise = [](std::vector<float>& v) {
    float peak = 0.0f;
    for (size_t i = 4800; i < v.size(); ++i) peak = std::max(peak, std::fabs(v[i]));
    for (float& s : v) s /= peak;
  };
  normalise(a);
  normalise(b);
  double diff = 0.0;
  for (size_t i = 4800; i < a.size(); ++i) diff += double(a[i] - b[i]) * double(a[i] - b[i]);
  diff = std::sqrt(diff / double(a.size() - 4800));
  INFO("normalised RMS difference " << diff);
  REQUIRE(diff > 0.01);
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
