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
#include "engine/elements_engine.h"
#include "engine/native_rate_wrapper.h"
#include "stmlib/utils/random.h"
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
  REQUIRE(info.paramCount == 16);
  REQUIRE(info.usesInput);
  const char* names[16] = {"Model",       "Contour",       "Bow",         "Blow",
                           "Strike",      "Flow",          "Mallet",      "Bow Timbre",
                           "Blow Timbre", "Strike Timbre", "Geometry",    "Brightness",
                           "Damping",     "Position",      "Space",       "Ext In"};
  for (int i = 0; i < 16; ++i) {
    INFO(i);
    REQUIRE(std::string(info.params[i].name) == names[i]);
  }
  char buf[64];
  info.params[0].format(info.params[0].defaultValue, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Modal");
  REQUIRE(ElementsEngine::modelIndex(1.0f) == 2);
  info.params[15].format(info.params[15].defaultValue, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Strike");
  const char* routes[4] = {"Off", "Blow", "Strike", "Both"};
  for (int i = 0; i < 4; ++i) {
    info.params[15].format((float(i) + 0.5f) / 4.0f, buf, sizeof(buf));
    REQUIRE(std::string(buf) == routes[i]);
    REQUIRE(ElementsEngine::extInIndex(float(i) / 4.0f) == i);
  }
  REQUIRE(ElementsEngine::extInIndex(1.0f) == 3);
  info.params[1].format(info.params[1].defaultValue, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "100%");
}

TEST_CASE("Modal Synth defaults follow the Elements firmware") {
  const ParamValues p = defaults();
  const float expected[16] = {0.5f / 3.0f, 1.0f, 0.0f, 0.0f, 0.8f, 0.5f, 0.5f, 0.5f,
                              0.5f,        0.5f, 0.2f, 0.5f, 0.25f, 0.3f, 0.5f, 2.5f / 4.0f};
  for (int i = 0; i < 16; ++i) {
    INFO(i);
    REQUIRE(p.p[i] == expected[i]);
  }
}

TEST_CASE("Modal Synth stays in tune through the wrapper") {
  const auto q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  for (double rate : {44100.0, 48000.0}) {
    NativeRateWrapper wrapped(std::make_unique<ElementsEngine>(), ElementsEngine::kNativeRate,
                              ElementsEngine::kNativeBlock);
    wrapped.setQuality(q);
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
  const double early = test::rms(tail.l, 0, 16000);
  const double late = test::rms(tail.l, 80000, 96000);
  INFO("early " << early << " late " << late);
  REQUIRE(late < 1e-3);
  REQUIRE(early > 100.0 * late);
  REQUIRE(test::rms(tail.r, 80000, 96000) < 1e-3);
}

TEST_CASE("Input excites the Modal Synth through its strike input") {
  ParamValues p = defaults();
  p.p[4] = 0.0f;  // internal strike off
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
      // Sweep every parameter, Ext In through all four routings included.
      for (int i = 1; i < 16; ++i) p.p[i] = float((block * 7 + i * 3) % 20) / 19.0f;
      engine.process(block % 2 ? input : nullptr, out, p, block % 3 ? 256 : 250);  // partial
      if (block == 75) engine.reset();  // re-snaps the smoothers
    }
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}

TEST_CASE("A repeated note-on re-excites the Modal Synth") {
  const ParamValues p = defaults();
  ElementsEngine engine;
  engine.prepare(ElementsEngine::kNativeRate, 256);
  engine.noteOn(60.0f, 1.0f);
  const auto first = render(engine, p, 32000);
  const double low = test::rms(first.l, 32000 - 3200, 32000);
  engine.noteOn(60.0f, 1.0f);
  const auto second = render(engine, p, 3200);
  const double high = test::rms(second.l, 0, 3200);
  INFO("tail " << low << " retrigger " << high);
  REQUIRE(high > 5.0 * low);
}

TEST_CASE("A note shorter than a block still sounds") {
  ElementsEngine engine;
  engine.prepare(ElementsEngine::kNativeRate, 256);
  engine.noteOn(60.0f, 1.0f);
  engine.noteOff();
  const auto out = render(engine, defaults(), 16000);
  REQUIRE(test::rms(out.l, 0, 16000) > 0.001);
}

TEST_CASE("Note-off then note-on before the next block re-excites") {
  const ParamValues p = defaults();
  ElementsEngine engine;
  engine.prepare(ElementsEngine::kNativeRate, 256);
  engine.noteOn(60.0f, 1.0f);
  const auto first = render(engine, p, 32000);
  const double low = test::rms(first.l, 32000 - 3200, 32000);
  engine.noteOff();
  engine.noteOn(60.0f, 1.0f);
  const auto second = render(engine, p, 3200);
  const double high = test::rms(second.l, 0, 3200);
  INFO("tail " << low << " retrigger " << high);
  REQUIRE(high > 5.0 * low);
}

namespace {

// rms of the sample-wise difference, relative to the reference's rms. Both renders start
// from the same reset and noise seed, so identical settings give exactly 0.
double relativeDifference(const Stereo& a, const Stereo& b) {
  double diff = 0.0, ref = 0.0;
  for (size_t i = 0; i < a.l.size(); ++i) {
    const double dl = double(a.l[i]) - double(b.l[i]);
    const double dr = double(a.r[i]) - double(b.r[i]);
    diff += dl * dl + dr * dr;
    ref += double(a.l[i]) * a.l[i] + double(a.r[i]) * a.r[i];
  }
  return ref > 0.0 ? std::sqrt(diff / ref) : 0.0;
}

// One second of a held note with all three exciters on, so every exciter control matters.
// The bow, blow and string noise come from stmlib's shared RNG, so each render reseeds it.
Stereo renderHeld(const ParamValues& p) {
  stmlib::Random::Seed(0x21);
  ElementsEngine engine;
  engine.prepare(ElementsEngine::kNativeRate, 256);
  engine.noteOn(57.0f, 0.8f);
  return render(engine, p, 32000);
}

ParamValues allExciters() {
  ParamValues p = defaults();
  p.p[2] = 0.6f;  // Bow
  p.p[3] = 0.6f;  // Blow
  return p;
}

std::vector<float> noise(size_t n, float gain) {
  std::vector<float> x(n);
  uint32_t seed = 7;
  for (float& s : x) {
    seed = seed * 1664525u + 1013904223u;
    s = gain * float(int32_t(seed)) / 2147483648.0f;
  }
  return x;
}

// held: play a note with the three internal exciters at zero, so any output comes from the
// input. Elements multiplies the blow input by the exciter envelope (dsp/voice.cc), so the Blow
// route is silent without a gate, as on the hardware.
// internal: keep the default internal Strike on during the held note.
Stereo renderInput(int extIn, const std::vector<float>* input, bool held = false,
                   bool internal = false) {
  ParamValues p = defaults();
  p.p[15] = (float(extIn) + 0.5f) / 4.0f;
  if (held && !internal) p.p[2] = p.p[3] = p.p[4] = 0.0f;  // Bow, Blow, Strike
  stmlib::Random::Seed(0x21);
  ElementsEngine engine;
  engine.prepare(ElementsEngine::kNativeRate, 256);
  if (held) engine.noteOn(57.0f, 0.8f);
  return render(engine, p, 16000, input);
}

double rmsStereo(const Stereo& s) {
  return std::sqrt(0.5 * (test::rms(s.l, 0, s.l.size()) * test::rms(s.l, 0, s.l.size()) +
                          test::rms(s.r, 0, s.r.size()) * test::rms(s.r, 0, s.r.size())));
}

}  // namespace

TEST_CASE("Each new Modal Synth control changes the sound") {
  const ParamValues base = allExciters();
  const Stereo reference = renderHeld(base);
  REQUIRE(relativeDifference(reference, renderHeld(base)) == 0.0);
  struct Case {
    const char* name;
    int index;
    float value;
  };
  const Case cases[] = {{"Contour", 1, 0.2f},       {"Flow", 5, 0.1f},
                        {"Mallet", 6, 0.1f},        {"Bow Timbre", 7, 0.9f},
                        {"Blow Timbre", 8, 0.9f},   {"Strike Timbre", 9, 0.9f},
                        {"Space", 14, 0.9f}};
  for (const Case& c : cases) {
    ParamValues p = base;
    p.p[c.index] = c.value;
    const double d = relativeDifference(reference, renderHeld(p));
    INFO(c.name << " relative difference " << d);
    REQUIRE(d > 0.05);
  }
}

TEST_CASE("Ext In routes the input to the Blow and Strike exciters") {
  const std::vector<float> in = noise(16000, 0.3f);

  // No note: Off is silent, Strike and Both ring from the input alone, Blow waits for a gate.
  const double off = rmsStereo(renderInput(0, &in));
  const double blowNoGate = rmsStereo(renderInput(1, &in));
  const double strike = rmsStereo(renderInput(2, &in));
  const double both = rmsStereo(renderInput(3, &in));
  INFO("no note: off " << off << " blow " << blowNoGate << " strike " << strike << " both "
                       << both);
  REQUIRE(off < 1e-4);
  REQUIRE(blowNoGate < 1e-4);
  REQUIRE(strike > 0.01);
  REQUIRE(both > 0.01);

  // Held note, internal exciters at zero: only routed input sounds, and Blow and Strike
  // excite the resonator differently.
  const Stereo heldOff = renderInput(0, &in, true);
  const Stereo heldBlow = renderInput(1, &in, true);
  const Stereo heldStrike = renderInput(2, &in, true);
  const Stereo heldBoth = renderInput(3, &in, true);
  const double blowVsStrike = relativeDifference(heldStrike, heldBlow);
  INFO("held: off " << rmsStereo(heldOff) << " blow " << rmsStereo(heldBlow) << " strike "
                    << rmsStereo(heldStrike) << " both " << rmsStereo(heldBoth)
                    << " blow vs strike " << blowVsStrike);
  REQUIRE(rmsStereo(heldOff) < 1e-4);
  REQUIRE(rmsStereo(heldBlow) > 0.01);
  REQUIRE(rmsStereo(heldStrike) > 0.01);
  REQUIRE(rmsStereo(heldBoth) > 0.01);
  REQUIRE(blowVsStrike > 0.5);
}

TEST_CASE("Ext In with no input adds nothing") {
  // A held note with the default internal Strike sounds; with a null input every route must
  // match Off routing of a real input, i.e. the input contributes nothing.
  const std::vector<float> in = noise(16000, 0.3f);
  const Stereo offWithInput = renderInput(0, &in, true, true);
  REQUIRE(rmsStereo(offWithInput) > 0.01);
  for (int route = 0; route < 4; ++route) {
    INFO(route);
    REQUIRE(rmsStereo(renderInput(route, nullptr)) < 1e-4);
    const Stereo nullInput = renderInput(route, nullptr, true, true);
    REQUIRE(nullInput.l == offWithInput.l);
    REQUIRE(nullInput.r == offWithInput.r);
  }
}

TEST_CASE("Modal Synth smooths its continuous controls like the firmware") {
  // CvScaler: every pot gets a 0.01 one-pole per 16-sample block; Flow, Mallet and Geometry
  // add a 0.05 one-pole, Position and Space a 0.01 one-pole (cv_scaler.cc:148-150, 178-186).
  ElementsEngine engine;
  engine.prepare(ElementsEngine::kNativeRate, 256);
  std::vector<float> l(16), r(16);
  float* out[2] = {l.data(), r.data()};
  ParamValues p = defaults();
  p.p[14] = 0.0f;  // Space
  engine.process(nullptr, out, p, 16);
  REQUIRE(engine.smoothedParam(14) == 0.0f);  // first render after reset snaps

  // One block after a step: general pot filter only, or that followed by BIND's filter.
  ParamValues step = p;
  step.p[1] = 0.0f;   // Contour 1 -> 0
  step.p[5] = 1.0f;   // Flow 0.5 -> 1
  step.p[11] = 1.0f;  // Brightness 0.5 -> 1 (clamped at 0.9995)
  step.p[14] = 1.0f;  // Space 0 -> 1
  engine.process(nullptr, out, step, 16);
  REQUIRE_THAT(engine.smoothedParam(1), Catch::Matchers::WithinAbs(0.99, 1e-6));
  REQUIRE_THAT(engine.smoothedParam(5), Catch::Matchers::WithinAbs(0.5 + 0.05 * 0.005, 1e-6));
  REQUIRE_THAT(engine.smoothedParam(11), Catch::Matchers::WithinAbs(0.505, 1e-6));
  REQUIRE_THAT(engine.smoothedParam(14), Catch::Matchers::WithinAbs(0.0001, 1e-7));

  // Space keeps rising gradually: no block moves it more than one 0.01 pole can, and the
  // two cascaded 50 ms poles (0.99 per block at 2 kHz) reach ~27% after 100 blocks and
  // settle within 1000 blocks (0.5 s).
  float previous = engine.smoothedParam(14);
  float at100 = 0.0f;
  for (int block = 2; block <= 1000; ++block) {
    engine.process(nullptr, out, step, 16);
    const float v = engine.smoothedParam(14);
    INFO("block " << block << " value " << v);
    REQUIRE(v >= previous);
    REQUIRE(v - previous <= 0.01f);
    if (block == 100) at100 = v;
    previous = v;
  }
  INFO("after 100 blocks " << at100 << ", after 1000 " << previous);
  REQUIRE(at100 > 0.2f);
  REQUIRE(at100 < 0.35f);
  REQUIRE(previous > 0.999f);

  // A host block of 64 samples smooths like four native blocks.
  ElementsEngine a, b;
  a.prepare(ElementsEngine::kNativeRate, 256);
  b.prepare(ElementsEngine::kNativeRate, 256);
  std::vector<float> l64(64), r64(64);
  float* out64[2] = {l64.data(), r64.data()};
  a.process(nullptr, out, p, 16);
  b.process(nullptr, out, p, 16);
  a.process(nullptr, out64, step, 64);
  for (int i = 0; i < 4; ++i) b.process(nullptr, out, step, 16);
  REQUIRE(a.smoothedParam(14) == b.smoothedParam(14));

  // A partial block advances the filter by its share of a native block.
  ElementsEngine c;
  c.prepare(ElementsEngine::kNativeRate, 256);
  c.process(nullptr, out, p, 16);
  c.process(nullptr, out, step, 8);
  c.process(nullptr, out, step, 8);
  const float halves = c.smoothedParam(1);
  REQUIRE_THAT(halves, Catch::Matchers::WithinAbs(0.99, 1e-5));

  // reset() snaps again on the next render.
  engine.reset();
  engine.process(nullptr, out, p, 16);
  REQUIRE(engine.smoothedParam(14) == 0.0f);
  REQUIRE(engine.smoothedParam(1) == 1.0f);
}
