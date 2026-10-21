#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "alloc_counter.h"
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

TEST_CASE("Macro Oscillator smooths its pots like the firmware") {
  // Ui::Poll runs once per 12-sample block (plaits.cc:81, dsp.h:51) and each PotController
  // applies ONE_POLE(value_, pot, c) (pot_controller.h:106): 0.005 for Frequency and
  // Harmonics, 0.01 for Timbre and Morph (ui.cc:72-79). Decay and LPG Colour are the Morph
  // and Timbre pots' hidden parameters, so they follow the same 0.01 pole.
  PlaitsEngine engine;
  engine.prepare(PlaitsEngine::kNativeRate, 512);
  std::vector<float> l(12), r(12);
  float* out[2] = {l.data(), r.data()};
  eg13::ParamValues p = defaultParams(PlaitsEngine::staticInfo());
  p.p[2] = 0.0f;  // Timbre
  engine.process(nullptr, out, p, 12);
  REQUIRE(engine.smoothedParam(2) == 0.0f);  // first render after reset snaps
  REQUIRE(engine.smoothedParam(1) == 0.5f);

  eg13::ParamValues step = p;
  step.p[0] = 0.99f;  // Model: stepped
  step.p[1] = 1.0f;   // Harmonics 0.5 -> 1
  step.p[2] = 1.0f;   // Timbre 0 -> 1
  step.p[3] = 0.0f;   // Morph 0.5 -> 0
  step.p[4] = 1.0f;   // Frequency 0.5 -> 1
  step.p[5] = 1.0f;   // Decay 0.5 -> 1
  step.p[6] = 0.0f;   // LPG Colour 0.5 -> 0
  step.p[7] = 1.0f;   // Aux Mix: EG13-only: not pot-smoothed (de-clicked per sample)
  engine.process(nullptr, out, step, 12);
  REQUIRE(engine.smoothedParam(0) == 0.99f);
  REQUIRE_THAT(engine.smoothedParam(1), Catch::Matchers::WithinAbs(0.5025, 1e-6));
  REQUIRE_THAT(engine.smoothedParam(2), Catch::Matchers::WithinAbs(0.01, 1e-6));
  REQUIRE_THAT(engine.smoothedParam(3), Catch::Matchers::WithinAbs(0.495, 1e-6));
  REQUIRE_THAT(engine.smoothedParam(4), Catch::Matchers::WithinAbs(0.5025, 1e-6));
  REQUIRE_THAT(engine.smoothedParam(5), Catch::Matchers::WithinAbs(0.505, 1e-6));
  REQUIRE_THAT(engine.smoothedParam(6), Catch::Matchers::WithinAbs(0.495, 1e-6));
  REQUIRE(engine.smoothedParam(7) == 1.0f);

  // Gradual: no block moves Timbre more than one 0.01 pole can (0.01 of the full step) or
  // Frequency more than 0.005 of its step. Time constant: 0.99^n and 0.995^n remain.
  float timbre = engine.smoothedParam(2);
  float frequency = engine.smoothedParam(4);
  for (int block = 2; block <= 2000; ++block) {
    engine.process(nullptr, out, step, 12);
    const float t = engine.smoothedParam(2);
    const float f = engine.smoothedParam(4);
    INFO("block " << block << " timbre " << t << " frequency " << f);
    REQUIRE(t >= timbre);
    REQUIRE(t - timbre <= 0.01f);
    REQUIRE(f >= frequency);
    REQUIRE(f - frequency <= 0.5f * 0.005f);
    if (block == 100) {
      REQUIRE_THAT(t, Catch::Matchers::WithinAbs(1.0 - std::pow(0.99, 100), 1e-4));
      REQUIRE_THAT(f, Catch::Matchers::WithinAbs(1.0 - 0.5 * std::pow(0.995, 100), 1e-4));
    }
    timbre = t;
    frequency = f;
  }
  REQUIRE(timbre > 0.9999f);
  REQUIRE(frequency > 0.9999f);

  // reset() snaps again on the next render.
  engine.reset();
  engine.process(nullptr, out, step, 12);
  REQUIRE(engine.smoothedParam(2) == 1.0f);
  REQUIRE(engine.smoothedParam(3) == 0.0f);
}

TEST_CASE("Macro Oscillator smoothing is independent of block size") {
  const auto p = defaultParams(PlaitsEngine::staticInfo());
  eg13::ParamValues step = p;
  step.p[1] = 0.9f;
  step.p[2] = 0.1f;
  step.p[4] = 0.8f;
  step.p[5] = 0.2f;
  PlaitsEngine a, b, c;
  for (auto* e : {&a, &b, &c}) {
    e->prepare(PlaitsEngine::kNativeRate, 4096);
    e->noteOn(60.0f, 1.0f);
  }
  auto ra = render(a, p, 1000, 4096);
  auto rb = render(b, p, 1000, 1);
  auto rc = render(c, p, 1000, 7);
  const auto ra2 = render(a, step, 6000, 4096);
  const auto rb2 = render(b, step, 6000, 1);
  const auto rc2 = render(c, step, 6000, 7);
  ra.insert(ra.end(), ra2.begin(), ra2.end());
  rb.insert(rb.end(), rb2.begin(), rb2.end());
  rc.insert(rc.end(), rc2.begin(), rc2.end());
  REQUIRE(ra == rb);
  REQUIRE(ra == rc);
  REQUIRE(a.smoothedParam(4) == b.smoothedParam(4));
}

TEST_CASE("Macro Oscillator processing with moving pots does not allocate") {
  PlaitsEngine engine;
  engine.prepare(PlaitsEngine::kNativeRate, 512);
  std::vector<float> l(512), r(512);
  float* out[2] = {l.data(), r.data()};
  eg13::ParamValues p = defaultParams(PlaitsEngine::staticInfo());
  engine.noteOn(60.0f, 1.0f);
  engine.process(nullptr, out, p, 512);
  size_t allocations = 0;
  {
    eg13::test::ScopedAllocationCounter counter;
    for (int block = 0; block < 100; ++block) {
      p.p[1 + block % 7] = float(block % 7) / 6.0f;  // includes Aux Mix
      if (block == 50) engine.reset();
      engine.process(nullptr, out, p, 512);
    }
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}

TEST_CASE("Macro Oscillator smooths Aux Mix per sample") {
  // Aux Mix is EG13-only (OUT and AUX are separate jacks on the module), so it gets a ~5 ms
  // per-sample de-click one-pole instead of a firmware pot filter.
  const double rate = PlaitsEngine::kNativeRate;
  const float c = float(1.0 - std::exp(-1.0 / (0.005 * rate)));
  PlaitsEngine engine;
  engine.prepare(rate, 512);
  engine.noteOn(60.0f, 1.0f);
  eg13::ParamValues p = defaultParams(PlaitsEngine::staticInfo());
  render(engine, p, 600, 512);
  REQUIRE(engine.smoothedAuxMix() == 0.0f);  // snapped to the default
  eg13::ParamValues step = p;
  step.p[7] = 1.0f;
  float previous = engine.smoothedAuxMix();
  for (int i = 1; i <= 4000; ++i) {
    render(engine, step, 1, 1);
    const float v = engine.smoothedAuxMix();
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
  render(engine, step, 12, 12);
  REQUIRE(engine.smoothedAuxMix() == 1.0f);  // reset snaps on the next render
}

TEST_CASE("Macro Oscillator AUX is upstream Plaits' AUX output") {
  PlaitsEngine engine;
  REQUIRE(PlaitsEngine::staticInfo().hasAux);
  engine.setAuxEnabled(true);
  engine.prepare(PlaitsEngine::kNativeRate, 512);
  engine.noteOn(69.0f, 0.8f);
  const int blocks = 200;
  std::vector<float> l(size_t(blocks * 12)), r(l.size()), aux(l.size());
  const auto p = defaultParams(PlaitsEngine::staticInfo());
  for (int b = 0; b < blocks; ++b) {
    float* out[3] = {l.data() + b * 12, r.data() + b * 12, aux.data() + b * 12};
    engine.process(nullptr, out, p, 12);
  }

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
    mods.trigger = b == 0 ? 0.0f : 1.0f;
    reference->Render(patch, mods, frames, plaits::kBlockSize);
    for (size_t j = 0; j < plaits::kBlockSize; ++j) {
      REQUIRE(aux[size_t(b) * 12 + j] == float(frames[j].aux) / 32768.0f);
      REQUIRE(l[size_t(b) * 12 + j] == float(frames[j].out) / 32768.0f);  // Aux Mix 0: OUT only
    }
  }
}

TEST_CASE("Macro Oscillator AUX does not change the main outputs") {
  PlaitsEngine a, b;
  b.setAuxEnabled(true);
  for (auto* e : {&a, &b}) {
    e->prepare(PlaitsEngine::kNativeRate, 512);
    e->noteOn(60.0f, 1.0f);
  }
  const auto p = defaultParams(PlaitsEngine::staticInfo());
  std::vector<float> la(480), ra(480), lb(480), rb(480), xb(480);
  float* outA[2] = {la.data(), ra.data()};
  float* outB[3] = {lb.data(), rb.data(), xb.data()};
  a.process(nullptr, outA, p, 480);
  b.process(nullptr, outB, p, 480);
  REQUIRE(la == lb);
  REQUIRE(ra == rb);
}
