#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "alloc_counter.h"
#include "engine/audio_input_engine.h"
#include "test_helpers.h"

using namespace eg13;

namespace {

std::vector<float> run(AudioInputEngine& engine, const ParamValues& p, const std::vector<float>& in) {
  std::vector<float> l(in.size()), r(in.size());
  for (size_t done = 0; done < in.size(); done += 256) {
    const int count = int(std::min<size_t>(256, in.size() - done));
    const float* input[2] = {in.data() + done, in.data() + done};
    float* out[2] = {l.data() + done, r.data() + done};
    engine.process(input, out, p, count);
  }
  return l;
}

ParamValues defaults() { return test::defaultParams(AudioInputEngine::staticInfo()); }

}  // namespace

TEST_CASE("Audio Input describes itself") {
  const auto& info = AudioInputEngine::staticInfo();
  REQUIRE(std::string(info.id) == "input");
  REQUIRE(std::string(info.displayName) == "Audio Input");
  REQUIRE(info.paramCount == 3);
  REQUIRE(info.usesInput);
  REQUIRE_THAT(AudioInputEngine::gainDb(info.params[0].defaultValue), Catch::Matchers::WithinAbs(0.0, 1e-4));
  REQUIRE_THAT(AudioInputEngine::attackSeconds(info.params[1].defaultValue), Catch::Matchers::WithinRel(0.005, 1e-3));
  REQUIRE_THAT(AudioInputEngine::releaseSeconds(info.params[2].defaultValue), Catch::Matchers::WithinRel(0.05, 1e-3));
}

TEST_CASE("Audio Input passes the input at unity while the gate is on") {
  AudioInputEngine engine;
  engine.prepare(48000.0, 256);
  engine.noteOn(60.0f, 1.0f);
  std::vector<float> in(48000, 0.5f);
  const auto out = run(engine, defaults(), in);
  for (size_t i = 24000; i < 48000; ++i) REQUIRE(std::fabs(out[i] - 0.5f) < 1e-5f);
}

TEST_CASE("Audio Input retargets the envelope on a note-on while the gate is on") {
  AudioInputEngine engine;
  engine.prepare(48000.0, 256);
  engine.noteOn(60.0f, 1.0f);
  run(engine, defaults(), std::vector<float>(24000, 0.5f));
  engine.noteOn(62.0f, 0.5f);
  const auto out = run(engine, defaults(), std::vector<float>(48000, 0.5f));
  REQUIRE(std::fabs(out[47999] - 0.25f) < 1e-5f);
}

TEST_CASE("Audio Input closes after release") {
  AudioInputEngine engine;
  engine.prepare(48000.0, 256);
  engine.noteOn(60.0f, 1.0f);
  std::vector<float> in(24000, 0.5f);
  run(engine, defaults(), in);
  engine.noteOff();
  const auto out = run(engine, defaults(), std::vector<float>(48000, 0.5f));
  REQUIRE(std::fabs(out[47999]) < 1e-5f);
}

TEST_CASE("Audio Input gain") {
  ParamValues p = defaults();
  p.p[0] = 1.0f;  // +12 dB
  AudioInputEngine engine;
  engine.prepare(48000.0, 256);
  engine.noteOn(60.0f, 1.0f);
  const auto out = run(engine, p, std::vector<float>(24000, 0.1f));
  REQUIRE_THAT(double(out[23999]), Catch::Matchers::WithinRel(0.1 * std::pow(10.0, 12.0 / 20.0), 1e-3));
}

TEST_CASE("Audio Input without input is silent and survives NaN parameters") {
  ParamValues bad;
  for (float& v : bad.p) v = std::numeric_limits<float>::quiet_NaN();
  AudioInputEngine engine;
  engine.prepare(48000.0, 256);
  engine.noteOn(60.0f, 1.0f);
  std::vector<float> l(256), r(256);
  float* out[2] = {l.data(), r.data()};
  engine.process(nullptr, out, bad, 256);
  for (float s : l) REQUIRE(s == 0.0f);
  const auto withInput = run(engine, bad, std::vector<float>(4800, 0.5f));
  for (float s : withInput) REQUIRE(std::isfinite(s));
}

TEST_CASE("Audio Input does not allocate") {
  AudioInputEngine engine;
  engine.prepare(48000.0, 256);
  std::vector<float> in(256, 0.2f), l(256), r(256);
  const float* input[2] = {in.data(), in.data()};
  float* out[2] = {l.data(), r.data()};
  const ParamValues p = defaults();
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    engine.reset();
    engine.noteOn(60.0f, 1.0f);
    for (int block = 0; block < 50; ++block) engine.process(input, out, p, 256);
    engine.noteOff();
    for (int block = 0; block < 50; ++block) engine.process(input, out, p, 256);
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}

TEST_CASE("Audio Input smooths Gain steps without a click") {
  // Gain is a pure multiplier on the input, so a step at a control-rate boundary is a step
  // in the output. A 1 ms one-pole on the linear gain turns it into a short ramp.
  AudioInputEngine engine;
  const double rate = 48000.0;
  engine.prepare(rate, 256);
  ParamValues p = defaults();
  p.p[1] = 0.0f;  // 1 ms attack
  engine.noteOn(60.0f, 1.0f);
  const std::vector<float> dc(4800, 0.25f);
  const auto settled = run(engine, p, dc);
  REQUIRE_THAT(settled.back(), Catch::Matchers::WithinAbs(0.25, 1e-6));  // snapped: 0 dB

  ParamValues loud = p;
  loud.p[0] = 1.0f;  // +12 dB: 3.98x
  const auto out = run(engine, loud, dc);
  const float target = 0.25f * std::pow(10.0f, 12.0f / 20.0f);
  float previous = settled.back();
  for (size_t i = 0; i < out.size(); ++i) {
    INFO("sample " << i << " value " << out[i]);
    REQUIRE(out[i] >= previous);
    // One 1 ms pole step at 48 kHz moves at most 1 - exp(-1/48) ~ 2.1% of the distance.
    REQUIRE(out[i] - previous <= 0.021f * (target - 0.25f) + 1e-6f);
    previous = out[i];
  }
  // Converges within a few time constants: e^-5 of the step remains after 5 ms.
  REQUIRE_THAT(out[240], Catch::Matchers::WithinAbs(target, (target - 0.25f) * 0.0068f + 1e-5f));
  REQUIRE_THAT(out.back(), Catch::Matchers::WithinAbs(target, 1e-5));

  // reset() snaps the gain on the next render.
  engine.reset();
  engine.noteOn(60.0f, 1.0f);
  const auto again = run(engine, loud, dc);
  REQUIRE_THAT(again.back(), Catch::Matchers::WithinAbs(target, 1e-5));
  REQUIRE(engine.smoothedGain() == std::pow(10.0f, 12.0f / 20.0f));
}

TEST_CASE("Audio Input gain smoothing is independent of block size") {
  AudioInputEngine a, b;
  for (auto* e : {&a, &b}) {
    e->prepare(48000.0, 512);
    e->noteOn(60.0f, 1.0f);
  }
  std::vector<float> in(2048);
  for (size_t i = 0; i < in.size(); ++i) in[i] = std::sin(0.01f * float(i));
  ParamValues p = defaults();
  ParamValues loud = p;
  loud.p[0] = 0.9f;
  std::vector<float> la(2048), ra(2048), lb(2048), rb(2048);
  const float* inA[2] = {in.data(), in.data()};
  float* outA[2] = {la.data(), ra.data()};
  a.process(inA, outA, p, 512);
  const float* inA2[2] = {in.data() + 512, in.data() + 512};
  float* outA2[2] = {la.data() + 512, ra.data() + 512};
  a.process(inA2, outA2, loud, 1536);
  for (int done = 0; done < 2048; done += 16) {
    const float* inB[2] = {in.data() + done, in.data() + done};
    float* outB[2] = {lb.data() + done, rb.data() + done};
    b.process(inB, outB, done < 512 ? p : loud, 16);
  }
  REQUIRE(la == lb);
  REQUIRE(ra == rb);
}
