#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "engine/plaits_engine.h"
#include "plaits/dsp/voice.h"
#include "test_helpers.h"

using eg13::PlaitsEngine;
using eg13::test::defaultParams;

namespace {

std::vector<float> render(PlaitsEngine& engine, const eg13::ParamValues& p, int n,
                          int chunk) {
  std::vector<float> left(static_cast<size_t>(n)), right(static_cast<size_t>(n));
  for (int done = 0; done < n;) {
    const int count = std::min(chunk, n - done);
    float* out[2] = {left.data() + done, right.data() + done};
    engine.process(nullptr, out, p, count);
    done += count;
  }
  return left;
}

}  // namespace

TEST_CASE("Macro Oscillator describes itself") {
  const auto& info = PlaitsEngine::staticInfo();
  REQUIRE(std::string(info.id) == "plaits");
  REQUIRE(std::string(info.displayName) == "Macro Oscillator");
  REQUIRE(info.paramCount == 8);
  REQUIRE(std::string(info.params[0].name) == "Model");
  REQUIRE(PlaitsEngine::modelIndex(info.params[0].defaultValue) == 8);
  char buf[64];
  info.params[0].format(info.params[0].defaultValue, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Virtual analog");
}

TEST_CASE("Macro Oscillator matches upstream Plaits bit for bit at its native rate") {
  PlaitsEngine engine;
  engine.prepare(PlaitsEngine::kNativeRate, 512);
  engine.noteOn(69.0f, 0.8f);
  const int blocks = 400;
  const std::vector<float> actual =
      render(engine, defaultParams(PlaitsEngine::staticInfo()), blocks * 12, 512);

  static char ram[16384];
  stmlib::BufferAllocator allocator(ram, sizeof(ram));
  auto reference = std::make_unique<plaits::Voice>();
  reference->Init(&allocator);
  plaits::Patch patch = {};
  patch.note = 69.0f;
  patch.harmonics = 0.5f;
  patch.timbre = 0.5f;
  patch.morph = 0.5f;
  patch.engine = 8;
  patch.decay = 0.5f;
  patch.lpg_colour = 0.5f;
  plaits::Modulations mods = {};
  mods.trigger_patched = true;
  mods.level_patched = true;
  mods.level = 0.8f;
  plaits::Voice::Frame frames[plaits::kBlockSize];
  for (int b = 0; b < blocks; ++b) {
    mods.trigger = b == 0 ? 0.0f : 1.0f;  // adapter holds trigger low for one block on note-on
    reference->Render(patch, mods, frames, plaits::kBlockSize);
    for (size_t j = 0; j < plaits::kBlockSize; ++j) {
      REQUIRE(actual[size_t(b) * 12 + j] == float(frames[j].out) / 32768.0f);
    }
  }
}

TEST_CASE("Macro Oscillator tracks pitch at other host rates") {
  for (double rate : {44100.0, 48000.0, 96000.0}) {
    PlaitsEngine engine;
    engine.prepare(rate, 512);
    engine.noteOn(69.0f, 1.0f);
    const int n = int(rate * 1.5);
    const auto out = render(engine, defaultParams(PlaitsEngine::staticInfo()), n, 512);
    const double f = eg13::test::estimateFrequency(out, rate, size_t(rate * 0.5), size_t(n));
    INFO("rate " << rate << " measured " << f);
    REQUIRE_THAT(f, Catch::Matchers::WithinRel(440.0, 0.001));
  }
}

TEST_CASE("Macro Oscillator output is independent of block size") {
  const auto p = defaultParams(PlaitsEngine::staticInfo());
  PlaitsEngine a, b, c;
  for (auto* e : {&a, &b, &c}) {
    e->prepare(44100.0, 4096);
    e->noteOn(60.0f, 1.0f);
  }
  const auto ra = render(a, p, 9000, 4096);
  const auto rb = render(b, p, 9000, 1);
  const auto rc = render(c, p, 9000, 7);
  REQUIRE(ra == rb);
  REQUIRE(ra == rc);
}

TEST_CASE("Macro Oscillator parameter values are clamped") {
  REQUIRE(PlaitsEngine::modelIndex(1.0f) == 23);
  REQUIRE(PlaitsEngine::modelIndex(-0.1f) == 0);
  REQUIRE(PlaitsEngine::modelIndex(std::numeric_limits<float>::quiet_NaN()) == 0);
  REQUIRE(PlaitsEngine::frequencyOffset(2.0f) == 12.0f);
  REQUIRE(PlaitsEngine::frequencyOffset(0.5f) == 0.0f);

  eg13::ParamValues bad;
  for (float& v : bad.p) v = std::numeric_limits<float>::quiet_NaN();
  PlaitsEngine engine;
  engine.prepare(48000.0, 512);
  engine.noteOn(60.0f, 1.0f);
  const auto out = render(engine, bad, 4800, 512);
  for (float s : out) REQUIRE(std::isfinite(s));
}

TEST_CASE("Macro Oscillator follows a new note while the gate is held") {
  PlaitsEngine engine;
  engine.prepare(48000.0, 512);
  const auto p = defaultParams(PlaitsEngine::staticInfo());
  engine.noteOn(60.0f, 1.0f);
  render(engine, p, 24000, 512);
  engine.noteOn(67.0f, 1.0f);
  const auto out = render(engine, p, 48000, 512);
  const double f = eg13::test::estimateFrequency(out, 48000.0, 12000, 48000);
  REQUIRE_THAT(f, Catch::Matchers::WithinRel(392.0, 0.01));
}

TEST_CASE("Macro Oscillator goes quiet after note-off") {
  PlaitsEngine engine;
  engine.prepare(48000.0, 512);
  auto p = defaultParams(PlaitsEngine::staticInfo());
  p.p[5] = 0.0f;  // Decay: shortest LPG release
  engine.noteOn(60.0f, 1.0f);
  REQUIRE(eg13::test::rms(render(engine, p, 24000, 512), 0, 24000) > 0.01);
  engine.noteOff();
  const auto tail = render(engine, p, 48000, 512);
  REQUIRE(eg13::test::rms(tail, 43200, 48000) < 1e-3);
}

TEST_CASE("Macro Oscillator setNote changes pitch without a new note") {
  PlaitsEngine engine;
  engine.prepare(48000.0, 512);
  const auto p = defaultParams(PlaitsEngine::staticInfo());
  engine.noteOn(60.0f, 1.0f);
  render(engine, p, 24000, 512);
  engine.setNote(72.0f);
  const auto out = render(engine, p, 48000, 512);
  const double f = eg13::test::estimateFrequency(out, 48000.0, 12000, 48000);
  REQUIRE_THAT(f, Catch::Matchers::WithinRel(523.25, 0.01));
}
