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
