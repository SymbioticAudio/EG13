#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "Ripples/ripples.hpp"
#include "alloc_counter.h"
#include "engine/ripples_engine.h"
#include "reference/flagged_ripples.h"
#include "test_helpers.h"

using namespace eg13;

namespace {

ParamValues defaults() { return test::defaultParams(RipplesEngine::staticInfo()); }

std::vector<float> sine(size_t n, double hz, double rate, float amp = 0.5f) {
  std::vector<float> x(n);
  for (size_t i = 0; i < n; ++i) x[i] = amp * float(std::sin(6.283185307179586 * hz * double(i) / rate));
  return x;
}

std::vector<float> saw(size_t n, double hz, double rate, float amp = 0.5f) {
  std::vector<float> x(n);
  for (size_t i = 0; i < n; ++i) {
    const double phase = std::fmod(hz * double(i) / rate, 1.0);
    x[i] = amp * float(2.0 * phase - 1.0);
  }
  return x;
}

struct Stereo {
  std::vector<float> l, r;
};

Stereo render(RipplesEngine& e, const std::vector<float>& inL, const std::vector<float>& inR,
              const ParamValues& p, int chunk = 16) {
  Stereo s{std::vector<float>(inL.size()), std::vector<float>(inL.size())};
  for (size_t done = 0; done < inL.size();) {
    const int count = int(std::min<size_t>(size_t(chunk), inL.size() - done));
    const float* in[2] = {inL.data() + done, inR.data() + done};
    float* out[2] = {s.l.data() + done, s.r.data() + done};
    e.process(in, out, p, count);
    done += size_t(count);
  }
  return s;
}

// The reference: the bare VCV model built with Rack's flags, as VCV ships it.
using Reference = test::FlaggedRipples;

// The model's output as the adapter delivers it: polarity restored, volts to EG13 units.
template <typename Frame>
float delivered(const Frame& f, int mode) {
  const float v = mode == 0 ? f.lp4 : mode == 1 ? f.lp2 : f.bp2;
  return -v / RipplesEngine::kVolts;
}

// The adapter's model (RipplesModel, eg13::rack_flags) and the reference (Reference,
// eg13::test::rack_flags) are both built with Rack's own -funsafe-math-optimizations (Rack
// compile.mk:17), each in its own TU and namespace, so neither can stand in for the other at link
// time. The same flags may still compile the same code differently in two TUs (contraction,
// reassociation), so they agree to 1e-5 rather than bit for bit (owner, 2026-10-20).
bool nearModel(float actual, float expected) {
  return std::fabs(actual - expected) <= 1e-5f * std::max(1.0f, std::fabs(expected));
}

}  // namespace

TEST_CASE("Analog Filter describes itself") {
  const auto& info = RipplesEngine::staticInfo();
  REQUIRE(std::string(info.id) == "ripples");
  REQUIRE(std::string(info.displayName) == "Analog Filter");
  REQUIRE(info.paramCount == 3);
  REQUIRE(info.usesInput);
  REQUIRE(info.sideInputs == 0);
  const char* const names[3] = {"Frequency", "Resonance", "Mode"};
  for (int i = 0; i < 3; ++i) REQUIRE(std::string(info.params[i].name) == names[i]);
  REQUIRE(info.params[0].defaultValue == 1.0f);  // Ripples.cpp:36: fully open
  REQUIRE(info.params[1].defaultValue == 0.0f);  // Ripples.cpp:35
  char buf[32];
  info.params[0].format(1.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "20.00 kHz");
  info.params[0].format(0.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "20 Hz");
  const char* const modes[3] = {"LP4", "LP2", "BP2"};
  for (int m = 0; m < 3; ++m) {
    info.params[2].format((float(m) + 0.5f) / 3.0f, buf, sizeof(buf));
    REQUIRE(std::string(buf) == modes[m]);
    REQUIRE(RipplesEngine::modeIndex((float(m) + 0.5f) / 3.0f) == m);
  }
  REQUIRE(RipplesEngine::modeIndex(info.params[2].defaultValue) == 0);
  REQUIRE(RipplesEngine::modeIndex(std::numeric_limits<float>::quiet_NaN()) == 0);
}

TEST_CASE("Analog Filter matches the VCV Ripples model to 1e-5") {
  for (double rate : {44100.0, 48000.0, 96000.0}) {
    for (int mode = 0; mode < 3; ++mode) {
      RipplesEngine engine;
      engine.prepare(rate, 256);
      ParamValues p = defaults();
      p.p[0] = 0.55f;
      p.p[1] = 0.7f;
      p.p[2] = (float(mode) + 0.5f) / 3.0f;
      const auto in = saw(4800, 110.0, rate);
      rack::random::seed(11);
      const Stereo out = render(engine, in, in, p);
      rack::random::seed(11);
      Reference ref;
      ref.setSampleRate(float(rate));
      Reference::Frame f = {};
      f.freq_knob = 0.55f;
      f.res_knob = 0.7f;
      INFO("rate " << rate << " mode " << mode);
      for (size_t i = 0; i < in.size(); ++i) {
        f.input = in[i] * RipplesEngine::kVolts;
        ref.process(f);
        REQUIRE(nearModel(out.l[i], delivered(f, mode)));
        REQUIRE(out.r[i] == out.l[i]);
      }
    }
  }
}

// Informational, not an adapter test: the flagged model against the same model built without
// the flags (this TU). The difference is compiler drift from -funsafe-math-optimizations, at
// about -90 dBFS (3.3e-5 measured, arm64 at 96 kHz), not adapter error; the 1e-5 test above
// pins the adapter.
TEST_CASE("Analog Filter: Rack's fast-math flags drift less than 5e-5 from a strict build") {
  auto near = [](float actual, float expected) {
    return std::fabs(actual - expected) <= 5e-5f * std::max(1.0f, std::fabs(expected));
  };
  for (double rate : {44100.0, 48000.0, 96000.0}) {
    for (int mode = 0; mode < 3; ++mode) {
      const auto in = saw(4800, 110.0, rate);
      Reference flagged;
      flagged.setSampleRate(float(rate));
      ripples::RipplesEngine strict;
      strict.setSampleRate(float(rate));
      Reference::Frame a = {};
      ripples::RipplesEngine::Frame b = {};
      a.freq_knob = b.freq_knob = 0.55f;
      a.res_knob = b.res_knob = 0.7f;
      INFO("rate " << rate << " mode " << mode);
      rack::random::seed(11);
      std::vector<float> fast(in.size());
      for (size_t i = 0; i < in.size(); ++i) {
        a.input = in[i] * RipplesEngine::kVolts;
        flagged.process(a);
        fast[i] = delivered(a, mode);
      }
      rack::random::seed(11);
      for (size_t i = 0; i < in.size(); ++i) {
        b.input = in[i] * RipplesEngine::kVolts;
        strict.process(b);
        REQUIRE(near(fast[i], delivered(b, mode)));
      }
    }
  }
}

TEST_CASE("Analog Filter ramps Frequency and Resonance across each block") {
  RipplesEngine engine;
  engine.prepare(48000.0, 256);
  const auto in = saw(64, 220.0, 48000.0);
  ParamValues a = defaults();
  a.p[0] = 0.2f;
  a.p[1] = 0.1f;
  ParamValues b = a;
  b.p[0] = 0.8f;
  b.p[1] = 0.6f;
  rack::random::seed(3);
  std::vector<float> out(64), right(64);
  for (int block = 0; block < 4; ++block) {
    const float* i2[2] = {in.data() + block * 16, in.data() + block * 16};
    float* o2[2] = {out.data() + block * 16, right.data() + block * 16};
    engine.process(i2, o2, block == 0 ? a : b, 16);
  }
  REQUIRE(engine.smoothedParam(0) == 0.8f);  // converged by the end of the step's block
  rack::random::seed(3);
  Reference ref;
  ref.setSampleRate(48000.f);
  Reference::Frame f = {};
  const float df = (0.8f - 0.2f) / 16.0f;
  const float dr = (0.6f - 0.1f) / 16.0f;
  for (int i = 0; i < 64; ++i) {
    if (i < 16) {  // first block after reset: snapped
      f.freq_knob = 0.2f;
      f.res_knob = 0.1f;
    } else if (i < 32) {  // the step, gradually
      const int k = i - 16;
      f.freq_knob = k == 15 ? 0.8f : 0.2f + df * float(k + 1);
      f.res_knob = k == 15 ? 0.6f : 0.1f + dr * float(k + 1);
    } else {
      f.freq_knob = 0.8f;
      f.res_knob = 0.6f;
    }
    f.input = in[size_t(i)] * RipplesEngine::kVolts;
    ref.process(f);
    REQUIRE(nearModel(out[size_t(i)], delivered(f, 0)));
  }
}

TEST_CASE("Analog Filter snaps to its parameters after reset") {
  RipplesEngine engine;
  engine.prepare(48000.0, 256);
  ParamValues a = defaults();
  a.p[0] = 0.9f;
  const auto in = saw(32, 220.0, 48000.0);
  render(engine, in, in, a);
  engine.reset();
  ParamValues b = defaults();
  b.p[0] = 0.3f;
  rack::random::seed(5);
  const Stereo out = render(engine, in, in, b);
  rack::random::seed(5);
  Reference ref;
  ref.setSampleRate(48000.f);
  Reference::Frame f = {};
  f.freq_knob = 0.3f;
  for (size_t i = 0; i < in.size(); ++i) {
    f.input = in[i] * RipplesEngine::kVolts;
    ref.process(f);
    REQUIRE(nearModel(out.l[i], delivered(f, 0)));
  }
}

TEST_CASE("Analog Filter Mode switches at once (stepped, not smoothed)") {
  RipplesEngine engine;
  engine.prepare(48000.0, 256);
  ParamValues lp4 = defaults();
  lp4.p[0] = 0.5f;
  ParamValues bp2 = lp4;
  bp2.p[2] = 2.5f / 3.0f;
  const auto in = saw(32, 220.0, 48000.0);
  rack::random::seed(9);
  std::vector<float> out(32), right(32);
  const float* a[2] = {in.data(), in.data()};
  float* o[2] = {out.data(), right.data()};
  engine.process(a, o, lp4, 16);
  const float* b[2] = {in.data() + 16, in.data() + 16};
  float* ob[2] = {out.data() + 16, right.data() + 16};
  engine.process(b, ob, bp2, 16);
  rack::random::seed(9);
  Reference ref;
  ref.setSampleRate(48000.f);
  Reference::Frame f = {};
  f.freq_knob = 0.5f;
  for (int i = 0; i < 32; ++i) {
    f.input = in[size_t(i)] * RipplesEngine::kVolts;
    ref.process(f);
    REQUIRE(nearModel(out[size_t(i)], delivered(f, i < 16 ? 0 : 2)));
  }
}

TEST_CASE("Analog Filter open keeps the input's level and polarity") {
  RipplesEngine engine;
  engine.prepare(48000.0, 256);
  const auto in = sine(48000, 100.0, 48000.0);
  const Stereo out = render(engine, in, in, defaults());
  double dot = 0.0;
  for (size_t i = 4800; i < in.size(); ++i) dot += double(in[i]) * double(out.l[i]);
  REQUIRE(dot > 0.0);
  const double gainDb = 20.0 * std::log10(test::rms(out.l, 4800, 48000) / test::rms(in, 4800, 48000));
  INFO("open LP4 gain " << gainDb << " dB");
  REQUIRE(std::fabs(gainDb) < 1.0);
}

TEST_CASE("Analog Filter Frequency sets the cutoff") {
  ParamValues p = defaults();
  p.p[0] = 0.3f;  // ~160 Hz
  auto level = [&](double hz) {
    RipplesEngine engine;
    engine.prepare(48000.0, 256);
    const auto in = sine(24000, hz, 48000.0);
    const Stereo out = render(engine, in, in, p);
    return test::rms(out.l, 4800, 24000) / test::rms(in, 4800, 24000);
  };
  REQUIRE(level(40.0) > 0.7);
  REQUIRE(level(5000.0) < 0.02);  // measured during planning: 0.006 (-44 dB)
}

TEST_CASE("Analog Filter self-oscillates at full Resonance and stays bounded") {
  RipplesEngine engine;
  engine.prepare(48000.0, 256);
  ParamValues p = defaults();
  p.p[0] = 0.5f;
  p.p[1] = 1.0f;
  std::vector<float> kick(48000, 0.0f);
  kick[0] = 0.5f;  // a start, so the test does not wait on the model's 1e-6 V bootstrap noise
  const Stereo out = render(engine, kick, kick, p);
  for (float s : out.l) {
    REQUIRE(std::isfinite(s));
    REQUIRE(std::fabs(s) < 7.0f);  // opamps clamp at 10.6 V (ripples.hpp:112) times the LP4 gain
  }
  REQUIRE(test::rms(out.l, 24000, 48000) > 0.01);
}

TEST_CASE("Analog Filter runs its right instance only while stereo") {
  RipplesEngine engine;
  engine.prepare(48000.0, 256);
  ParamValues p = defaults();
  p.p[0] = 0.6f;
  const auto l = saw(960, 220.0, 48000.0);
  const auto r = saw(960, 330.0, 48000.0);
  engine.setStereo(false);
  const Stereo mono = render(engine, l, r, p);
  REQUIRE_FALSE(engine.rightRuns());
  REQUIRE(mono.l == mono.r);
  engine.setStereo(true);
  const Stereo stereo = render(engine, l, r, p);
  REQUIRE(engine.rightRuns());
  REQUIRE(stereo.l != stereo.r);
}

TEST_CASE("Analog Filter: stereo starts without a jump and stops with a fade") {
  RipplesEngine engine;
  engine.prepare(48000.0, 256);
  ParamValues p = defaults();
  p.p[0] = 0.5f;
  p.p[1] = 0.6f;
  const auto x = saw(4800, 110.0, 48000.0);
  render(engine, x, x, p);  // mono history
  engine.setStereo(true);   // same input on both sides: the right copy continues the left
  const Stereo start = render(engine, x, x, p);
  for (size_t i = 0; i < 64; ++i) REQUIRE(std::fabs(start.r[i] - start.l[i]) < 1e-4f);
  const auto y = saw(4800, 165.0, 48000.0);
  const Stereo apart = render(engine, x, y, p);
  REQUIRE(test::rms(apart.r, 2400, 4800) > 0.01);
  engine.setStereo(false);
  const Stereo stop = render(engine, x, y, p);
  float previousGap = std::fabs(stop.r[0] - stop.l[0]);
  for (size_t i = 0; i < 480; ++i) {
    const float gap = std::fabs(stop.r[i] - stop.l[i]);
    REQUIRE(std::fabs(gap - previousGap) < 0.1f);  // no step in R (a cut would jump by the whole gap)
    previousGap = gap;
  }
  for (size_t i = 496; i < stop.l.size(); ++i) REQUIRE(stop.r[i] == stop.l[i]);
  REQUIRE_FALSE(engine.rightRuns());
}

TEST_CASE("Analog Filter survives NaN parameters") {
  RipplesEngine engine;
  engine.prepare(48000.0, 256);
  ParamValues bad;
  for (float& v : bad.p) v = std::numeric_limits<float>::quiet_NaN();
  const auto x = saw(4800, 110.0, 48000.0);
  const Stereo out = render(engine, x, x, bad);
  for (float s : out.l) REQUIRE(std::isfinite(s));
}

TEST_CASE("Analog Filter processing does not allocate") {
  RipplesEngine engine;
  engine.prepare(48000.0, 256);
  std::vector<float> l(16, 0.1f), r(16, -0.1f), ol(16), orr(16);
  const float* in[2] = {l.data(), r.data()};
  float* out[2] = {ol.data(), orr.data()};
  ParamValues p = defaults();
  engine.process(in, out, p, 16);
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int block = 0; block < 300; ++block) {
      engine.setStereo(block % 100 < 50);
      p.p[0] = float(block % 10) / 10.0f;
      p.p[2] = float(block % 3) / 3.0f;
      engine.process(in, out, p, 16);
    }
    engine.reset();
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}
