#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "alloc_counter.h"
#include "engine/param_format.h"
#include "engine/svf_engine.h"
#include "stmlib/dsp/filter.h"
#include "test_helpers.h"

using namespace eg13;

namespace {

ParamValues defaults() { return test::defaultParams(SvfEngine::staticInfo()); }

std::vector<float> sine(size_t n, double hz, double rate) {
  std::vector<float> x(n);
  for (size_t i = 0; i < n; ++i) x[i] = 0.5f * float(std::sin(6.283185307179586 * hz * double(i) / rate));
  return x;
}

std::vector<float> saw(size_t n, double hz, double rate) {
  std::vector<float> x(n);
  for (size_t i = 0; i < n; ++i) x[i] = 0.5f * float(2.0 * std::fmod(hz * double(i) / rate, 1.0) - 1.0);
  return x;
}

struct Stereo {
  std::vector<float> l, r;
};

Stereo render(SvfEngine& e, const std::vector<float>& inL, const std::vector<float>& inR, const ParamValues& p) {
  Stereo s{std::vector<float>(inL.size()), std::vector<float>(inL.size())};
  for (size_t done = 0; done < inL.size(); done += 16) {
    const int count = int(std::min<size_t>(16, inL.size() - done));
    const float* in[2] = {inL.data() + done, inR.data() + done};
    float* out[2] = {s.l.data() + done, s.r.data() + done};
    e.process(in, out, p, count);
  }
  return s;
}

float runMode(stmlib::Svf& f, int mode, float x) {
  if (mode == 0) return f.Process<stmlib::FILTER_MODE_LOW_PASS>(x);
  if (mode == 1) return f.Process<stmlib::FILTER_MODE_BAND_PASS_NORMALIZED>(x);
  return f.Process<stmlib::FILTER_MODE_HIGH_PASS>(x);
}

double gainAt(double hz, const ParamValues& p) {
  SvfEngine engine;
  engine.prepare(48000.0, 256);
  const auto in = sine(24000, hz, 48000.0);
  const Stereo out = render(engine, in, in, p);
  return test::rms(out.l, 4800, 24000) / test::rms(in, 4800, 24000);
}

}  // namespace

TEST_CASE("State Variable Filter describes itself") {
  const auto& info = SvfEngine::staticInfo();
  REQUIRE(std::string(info.id) == "svf");
  REQUIRE(std::string(info.displayName) == "State Variable Filter");
  REQUIRE(info.paramCount == 3);
  REQUIRE(info.usesInput);
  const char* const names[3] = {"Cutoff", "Resonance", "Mode"};
  for (int i = 0; i < 3; ++i) REQUIRE(std::string(info.params[i].name) == names[i]);
  REQUIRE(info.params[0].defaultValue == 1.0f);
  REQUIRE(std::fabs(SvfEngine::resonanceQ(info.params[1].defaultValue) - 0.70710678f) < 1e-4f);
  REQUIRE(SvfEngine::resonanceQ(0.0f) == 0.5f);
  REQUIRE(std::fabs(SvfEngine::resonanceQ(1.0f) - 40.0f) < 1e-3f);
  char buf[32];
  const char* const modes[3] = {"LP", "BP", "HP"};
  for (int m = 0; m < 3; ++m) {
    info.params[2].format((float(m) + 0.5f) / 3.0f, buf, sizeof(buf));
    REQUIRE(std::string(buf) == modes[m]);
  }
}

TEST_CASE("State Variable Filter matches stmlib's Svf sample for sample") {
  for (int mode = 0; mode < 3; ++mode) {
    SvfEngine engine;
    engine.prepare(48000.0, 256);
    ParamValues p = defaults();
    p.p[0] = 0.55f;
    p.p[1] = 0.6f;
    p.p[2] = (float(mode) + 0.5f) / 3.0f;
    const auto in = saw(4800, 110.0, 48000.0);
    const Stereo out = render(engine, in, in, p);
    stmlib::Svf ref;
    ref.Init();
    const float f = std::min(knobHz(0.55f) / 48000.0f, SvfEngine::kMaxF);
    const float q = SvfEngine::resonanceQ(0.6f);
    for (size_t i = 0; i < in.size(); ++i) {
      ref.set_f_q<stmlib::FREQUENCY_ACCURATE>(f, q);
      INFO("mode " << mode << " sample " << i);
      REQUIRE(out.l[i] == runMode(ref, mode, in[i]));
      REQUIRE(out.r[i] == out.l[i]);
    }
  }
}

TEST_CASE("State Variable Filter ramps cutoff and Q across a block and snaps after reset") {
  SvfEngine engine;
  engine.prepare(48000.0, 256);
  const auto in = saw(48, 220.0, 48000.0);
  ParamValues a = defaults();
  a.p[0] = 0.3f;
  ParamValues b = a;
  b.p[0] = 0.7f;
  b.p[1] = 0.5f;
  std::vector<float> out(48), right(48);
  for (int block = 0; block < 3; ++block) {
    const float* i2[2] = {in.data() + block * 16, in.data() + block * 16};
    float* o2[2] = {out.data() + block * 16, right.data() + block * 16};
    engine.process(i2, o2, block == 0 ? a : b, 16);
  }
  const float fa = knobHz(0.3f) / 48000.0f, fb = knobHz(0.7f) / 48000.0f;
  const float qa = SvfEngine::resonanceQ(a.p[1]), qb = SvfEngine::resonanceQ(0.5f);
  const float df = (fb - fa) / 16.0f, dq = (qb - qa) / 16.0f;
  stmlib::Svf ref;
  ref.Init();
  for (int i = 0; i < 48; ++i) {
    float f = fb, q = qb;
    if (i < 16) {
      f = fa;
      q = qa;
    } else if (i < 31) {
      f = fa + df * float(i - 15);
      q = qa + dq * float(i - 15);
    }
    ref.set_f_q<stmlib::FREQUENCY_ACCURATE>(f, q);
    REQUIRE(out[size_t(i)] == ref.Process<stmlib::FILTER_MODE_LOW_PASS>(in[size_t(i)]));
  }
  REQUIRE(engine.smoothedParam(0) == fb);
  engine.reset();
  const float* i2[2] = {in.data(), in.data()};
  float* o2[2] = {out.data(), right.data()};
  engine.process(i2, o2, a, 16);
  stmlib::Svf fresh;
  fresh.Init();
  for (int i = 0; i < 16; ++i) {
    fresh.set_f_q<stmlib::FREQUENCY_ACCURATE>(fa, qa);
    REQUIRE(out[size_t(i)] == fresh.Process<stmlib::FILTER_MODE_LOW_PASS>(in[size_t(i)]));
  }
}

TEST_CASE("State Variable Filter modes pass and stop the right bands") {
  ParamValues p = defaults();
  p.p[0] = 0.5f;  // ~632 Hz
  REQUIRE(gainAt(60.0, p) > 0.9);
  REQUIRE(gainAt(8000.0, p) < 0.02);
  p.p[2] = 2.5f / 3.0f;  // HP
  REQUIRE(gainAt(60.0, p) < 0.02);
  REQUIRE(gainAt(8000.0, p) > 0.9);
  p.p[2] = 1.5f / 3.0f;  // BP, normalized: unity at the cutoff
  REQUIRE(std::fabs(gainAt(knobHz(0.5f), p) - 1.0) < 0.05);
  REQUIRE(gainAt(60.0, p) < 0.2);
}

TEST_CASE("State Variable Filter at its default resonance is -3 dB at the cutoff") {
  ParamValues p = defaults();
  p.p[0] = 0.5f;
  REQUIRE(std::fabs(20.0 * std::log10(gainAt(knobHz(0.5f), p)) + 3.0) < 0.3);
}

TEST_CASE("State Variable Filter runs its right instance only while stereo, starting seamlessly") {
  SvfEngine engine;
  engine.prepare(48000.0, 256);
  ParamValues p = defaults();
  p.p[0] = 0.5f;
  p.p[1] = 0.8f;
  const auto x = saw(4800, 110.0, 48000.0);
  const auto y = saw(4800, 165.0, 48000.0);
  const Stereo mono = render(engine, x, y, p);
  REQUIRE_FALSE(engine.rightRuns());
  REQUIRE(mono.l == mono.r);
  engine.setStereo(true);
  const Stereo same = render(engine, x, x, p);
  REQUIRE(engine.rightRuns());
  REQUIRE(same.l == same.r);  // a copied state with the same input stays identical
  const Stereo apart = render(engine, x, y, p);
  REQUIRE(apart.l != apart.r);
}

TEST_CASE("State Variable Filter survives NaN and processing does not allocate") {
  SvfEngine engine;
  engine.prepare(48000.0, 256);
  ParamValues bad;
  for (float& v : bad.p) v = std::numeric_limits<float>::quiet_NaN();
  const auto x = saw(480, 110.0, 48000.0);
  const Stereo out = render(engine, x, x, bad);
  for (float s : out.l) REQUIRE(std::isfinite(s));
  ParamValues p = defaults();
  std::vector<float> l(16, 0.1f), ol(16), orr(16);
  const float* in[2] = {l.data(), l.data()};
  float* o[2] = {ol.data(), orr.data()};
  size_t engineAllocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int block = 0; block < 200; ++block) {
      engine.setStereo(block % 50 < 25);
      p.p[0] = float(block % 10) / 10.0f;
      p.p[2] = float(block % 3) / 3.0f;
      engine.process(in, o, p, 16);
    }
    engine.reset();
    engineAllocations = counter.count();
  }
  REQUIRE(engineAllocations == 0);
}

TEST_CASE("State Variable Filter stays finite and bounded at extreme cutoff and Q") {
  const double rates[3] = {44100.0, 48000.0, 96000.0};
  for (double rate : rates) {
    for (int mode = 0; mode < 3; ++mode) {
      for (float cutoff : {0.0f, 0.5f, 1.0f}) {
        SvfEngine engine;
        engine.prepare(rate, 256);
        ParamValues p = defaults();
        p.p[0] = cutoff;
        p.p[1] = 1.0f;  // Q 40
        p.p[2] = (float(mode) + 0.5f) / 3.0f;
        const auto x = saw(9600, 110.0, rate);
        const auto s = sine(9600, double(knobHz(cutoff)), rate);
        for (const auto* in : {&x, &s}) {
          engine.reset();
          const Stereo out = render(engine, *in, *in, p);
          float peak = 0.0f;
          for (float v : out.l) {
            REQUIRE(std::isfinite(v));
            peak = std::max(peak, std::fabs(v));
          }
          INFO("rate " << rate << " mode " << mode << " cutoff " << cutoff);
          REQUIRE(peak < 100.0f);
        }
      }
    }
  }
}
