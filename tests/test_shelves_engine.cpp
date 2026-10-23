#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "Shelves/shelves.hpp"
#include "alloc_counter.h"
#include "engine/param_format.h"
#include "engine/shelves_engine.h"
#include "reference/flagged_shelves.h"
#include "test_helpers.h"

using namespace eg13;

namespace {

ParamValues defaults() { return test::defaultParams(ShelvesEngine::staticInfo()); }

std::vector<float> sine(size_t n, double hz, double rate, float amp = 0.25f) {
  std::vector<float> x(n);
  for (size_t i = 0; i < n; ++i) x[i] = amp * float(std::sin(6.283185307179586 * hz * double(i) / rate));
  return x;
}

struct Stereo {
  std::vector<float> l, r;
};

Stereo render(ShelvesEngine& e, const std::vector<float>& inL, const std::vector<float>& inR, const ParamValues& p) {
  Stereo s{std::vector<float>(inL.size()), std::vector<float>(inL.size())};
  for (size_t done = 0; done < inL.size(); done += 16) {
    const int count = int(std::min<size_t>(16, inL.size() - done));
    const float* in[2] = {inL.data() + done, inR.data() + done};
    float* out[2] = {s.l.data() + done, s.r.data() + done};
    e.process(in, out, p, count);
  }
  return s;
}

double gainDbAt(double hz, const ParamValues& p) {
  ShelvesEngine engine;
  engine.prepare(48000.0, 256);
  const auto in = sine(24000, hz, 48000.0);
  const Stereo out = render(engine, in, in, p);
  return 20.0 * std::log10(test::rms(out.l, 4800, 24000) / test::rms(in, 4800, 24000));
}

// The reference: the bare VCV model built with Rack's flags, as VCV ships it.
using Reference = test::FlaggedShelves;

// The model's frame for constant EG13 parameters (VCV's knob mapping, Shelves.cpp:125-136), for
// the flagged reference or the strict model (same field names).
template <typename Frame>
Frame frameFor(const ParamValues& p) {
  Frame f = {};
  f.hs_freq_knob = p.p[0];
  f.hs_gain_knob = 2.0f * p.p[1] - 1.0f;
  f.p1_freq_knob = p.p[2];
  f.p1_gain_knob = 2.0f * p.p[3] - 1.0f;
  f.p1_q_knob = p.p[4];
  f.p2_freq_knob = p.p[5];
  f.p2_gain_knob = 2.0f * p.p[6] - 1.0f;
  f.p2_q_knob = p.p[7];
  f.ls_freq_knob = p.p[8];
  f.ls_gain_knob = 2.0f * p.p[9] - 1.0f;
  f.pre_gain = ShelvesEngine::padOn(p.p[10]);
  const bool mid2 = ShelvesEngine::outputIndex(p.p[11]) >= 4;
  f.p2_lp_out_connected = f.p2_bp_out_connected = f.p2_hp_out_connected = mid2;
  return f;
}

// The adapter's model (ShelvesModel, eg13::rack_flags) and the reference (Reference,
// eg13::test::rack_flags) are both built with Rack's own -funsafe-math-optimizations (Rack
// compile.mk:17), each in its own TU and namespace, so neither can stand in for the other at link
// time. The same flags may still compile the same code differently in two TUs (contraction,
// reassociation), so they agree to 1e-5 rather than bit for bit (owner, 2026-10-20).
bool nearModel(float actual, float expected) {
  return std::fabs(actual - expected) <= 1e-5f * std::max(1.0f, std::fabs(expected));
}

template <typename Frame>
float tap(const Frame& f, int output) {
  switch (output) {
    case 1: return f.p1_lp_out;
    case 2: return f.p1_bp_out;
    case 3: return f.p1_hp_out;
    case 4: return f.p2_lp_out;
    case 5: return f.p2_bp_out;
    case 6: return f.p2_hp_out;
    default: return f.main_out;
  }
}

// A setting that moves every band, for the model comparisons.
ParamValues busy(float pad, int output) {
  ParamValues p = defaults();
  p.p[0] = 0.7f;
  p.p[1] = 0.2f;
  p.p[2] = 0.4f;
  p.p[3] = 0.8f;
  p.p[4] = 0.6f;
  p.p[5] = 0.6f;
  p.p[6] = 0.3f;
  p.p[7] = 0.3f;
  p.p[8] = 0.3f;
  p.p[9] = 0.9f;
  p.p[10] = pad;
  p.p[11] = (float(output) + 0.5f) / 7.0f;
  return p;
}

}  // namespace

TEST_CASE("EQ describes itself") {
  const auto& info = ShelvesEngine::staticInfo();
  REQUIRE(std::string(info.id) == "shelves");
  REQUIRE(std::string(info.displayName) == "EQ");
  REQUIRE(info.paramCount == 12);
  REQUIRE(info.usesInput);
  REQUIRE(info.sideInputs == 0);
  const char* const names[12] = {"High Freq", "High Gain", "Mid 1 Freq", "Mid 1 Gain", "Mid 1 Q", "Mid 2 Freq",
                                 "Mid 2 Gain", "Mid 2 Q", "Low Freq", "Low Gain", "Input Pad", "Output"};
  for (int i = 0; i < 12; ++i) REQUIRE(std::string(info.params[i].name) == names[i]);
  for (int i : {1, 3, 6, 9}) REQUIRE(ShelvesEngine::gainDb(info.params[i].defaultValue) == 0.0f);
  REQUIRE_FALSE(ShelvesEngine::padOn(info.params[10].defaultValue));
  REQUIRE(ShelvesEngine::outputIndex(info.params[11].defaultValue) == 0);
  REQUIRE(ShelvesEngine::qFactor(0.0f) == 0.5f);
  REQUIRE(std::fabs(ShelvesEngine::qFactor(1.0f) - 40.0f) < 1e-3f);
  char buf[32];
  info.params[1].format(1.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "+18.0 dB");
  info.params[11].format(2.5f / 7.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Mid 1 BP");
  info.params[10].format(0.75f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "-6 dB");
}

TEST_CASE("EQ matches the VCV Shelves model to 1e-5") {
  for (int output = 0; output < ShelvesEngine::kNumOutputs; ++output) {
    for (float pad : {0.25f, 0.75f}) {
      const ParamValues p = busy(pad, output);
      ShelvesEngine engine;
      engine.prepare(48000.0, 256);
      const auto in = sine(4800, 220.0, 48000.0, 0.5f);
      const Stereo out = render(engine, in, in, p);
      Reference ref;
      ref.setSampleRate(48000.f);
      auto f = frameFor<Reference::Frame>(p);
      INFO("output " << output << " pad " << pad);
      for (size_t i = 0; i < in.size(); ++i) {
        f.main_in = in[i] * ShelvesEngine::kVolts;
        ref.process(f);
        REQUIRE(nearModel(out.l[i], tap(f, output) / ShelvesEngine::kVolts));
        REQUIRE(out.r[i] == out.l[i]);
      }
    }
  }
}

// Informational, not an adapter test: the flagged model against the same model built without
// the flags (this TU). The difference is compiler drift from -funsafe-math-optimizations, at
// about -95 dBFS (1.7e-5 measured, arm64 macOS; 1.4e-5 x86_64, 1.1e-5 GCC 13 aarch64), not
// adapter error; the 1e-5 test above pins the adapter.
TEST_CASE("EQ: Rack's fast-math flags drift less than 5e-5 from a strict build") {
  auto near = [](float actual, float expected) {
    return std::fabs(actual - expected) <= 5e-5f * std::max(1.0f, std::fabs(expected));
  };
  for (double rate : {44100.0, 48000.0, 96000.0}) {
    for (int output = 0; output < ShelvesEngine::kNumOutputs; ++output) {
      for (float pad : {0.25f, 0.75f}) {
        const ParamValues p = busy(pad, output);
        const auto in = sine(4800, 220.0, rate, 0.5f);
        Reference flagged;
        flagged.setSampleRate(float(rate));
        shelves::ShelvesEngine strict;
        strict.setSampleRate(float(rate));
        auto a = frameFor<Reference::Frame>(p);
        auto b = frameFor<shelves::ShelvesEngine::Frame>(p);
        INFO("rate " << rate << " output " << output << " pad " << pad);
        for (size_t i = 0; i < in.size(); ++i) {
          a.main_in = b.main_in = in[i] * ShelvesEngine::kVolts;
          flagged.process(a);
          strict.process(b);
          REQUIRE(near(tap(a, output) / ShelvesEngine::kVolts, tap(b, output) / ShelvesEngine::kVolts));
        }
      }
    }
  }
}

TEST_CASE("EQ ramps its knobs across each block and snaps after reset") {
  ShelvesEngine engine;
  engine.prepare(48000.0, 256);
  const auto in = sine(48, 1000.0, 48000.0, 0.5f);
  ParamValues a = defaults();
  ParamValues b = a;
  b.p[1] = 0.9f;  // High Gain
  b.p[2] = 0.7f;  // Mid 1 Freq
  std::vector<float> out(48), right(48);
  for (int block = 0; block < 3; ++block) {
    const float* i2[2] = {in.data() + block * 16, in.data() + block * 16};
    float* o2[2] = {out.data() + block * 16, right.data() + block * 16};
    engine.process(i2, o2, block == 0 ? a : b, 16);
  }
  REQUIRE(engine.smoothedParam(1) == 0.9f);
  Reference ref;
  ref.setSampleRate(48000.f);
  auto f = frameFor<Reference::Frame>(a);
  const float dg = (0.9f - 0.5f) / 16.0f, df = (0.7f - 0.5f) / 16.0f;
  for (int i = 0; i < 48; ++i) {
    float gain = 0.9f, freq = 0.7f;
    if (i < 16) {
      gain = 0.5f;
      freq = 0.5f;
    } else if (i < 31) {
      gain = 0.5f + dg * float(i - 15);
      freq = 0.5f + df * float(i - 15);
    }
    f.hs_gain_knob = 2.0f * gain - 1.0f;
    f.p1_freq_knob = freq;
    f.main_in = in[size_t(i)] * ShelvesEngine::kVolts;
    ref.process(f);
    REQUIRE(nearModel(out[size_t(i)], f.main_out / ShelvesEngine::kVolts));
  }
  engine.reset();
  const float* i2[2] = {in.data(), in.data()};
  float* o2[2] = {out.data(), right.data()};
  engine.process(i2, o2, b, 16);
  Reference fresh;
  fresh.setSampleRate(48000.f);
  auto g = frameFor<Reference::Frame>(b);
  for (int i = 0; i < 16; ++i) {
    g.main_in = in[size_t(i)] * ShelvesEngine::kVolts;
    fresh.process(g);
    REQUIRE(nearModel(out[size_t(i)], g.main_out / ShelvesEngine::kVolts));
  }
}

TEST_CASE("EQ at its defaults is flat, in phase, and the pad lowers it by 6 dB") {
  const ParamValues p = defaults();
  for (double hz : {50.0, 1000.0, 8000.0}) REQUIRE(std::fabs(gainDbAt(hz, p)) < 0.2);
  ShelvesEngine engine;
  engine.prepare(48000.0, 256);
  const auto in = sine(4800, 1000.0, 48000.0);
  const Stereo out = render(engine, in, in, p);
  double dot = 0.0;
  for (size_t i = 480; i < in.size(); ++i) dot += double(in[i]) * double(out.l[i]);
  REQUIRE(dot > 0.0);
  ParamValues pad = p;
  pad.p[10] = 0.75f;
  REQUIRE(std::fabs(gainDbAt(1000.0, pad) + 6.02) < 0.2);
}

TEST_CASE("EQ shelves and bells move the right bands") {
  ParamValues low = defaults();
  low.p[9] = 1.0f;  // Low Gain +18 dB, corner ~632 Hz
  REQUIRE(std::fabs(gainDbAt(40.0, low) - 18.0) < 1.5);
  REQUIRE(std::fabs(gainDbAt(10000.0, low)) < 2.0);  // the shelf's tail: +1.7 dB measured during planning
  ParamValues high = defaults();
  high.p[1] = 0.0f;  // High Gain -18 dB
  REQUIRE(std::fabs(gainDbAt(15000.0, high) + 18.0) < 1.5);
  REQUIRE(std::fabs(gainDbAt(40.0, high)) < 1.0);
  ParamValues bell = defaults();
  bell.p[3] = 1.0f;  // Mid 1 +18 dB at ~632 Hz
  REQUIRE(gainDbAt(knobHz(0.5f), bell) > 15.0);
  REQUIRE(std::fabs(gainDbAt(40.0, bell)) < 2.0);
}

TEST_CASE("EQ filter-tap outputs pass their bands") {
  ParamValues p = defaults();
  p.p[11] = 1.5f / 7.0f;  // Mid 1 LP
  REQUIRE(gainDbAt(60.0, p) > gainDbAt(8000.0, p) + 30.0);
  p.p[11] = 3.5f / 7.0f;  // Mid 1 HP
  REQUIRE(gainDbAt(8000.0, p) > gainDbAt(60.0, p) + 30.0);
  p.p[11] = 5.5f / 7.0f;  // Mid 2 BP
  REQUIRE(gainDbAt(knobHz(0.5f), p) > gainDbAt(60.0, p) + 20.0);
}

TEST_CASE("EQ runs its right instance only while stereo, starting seamlessly") {
  ShelvesEngine engine;
  engine.prepare(48000.0, 256);
  ParamValues p = defaults();
  p.p[3] = 0.9f;
  const auto x = sine(4800, 440.0, 48000.0);
  const auto y = sine(4800, 660.0, 48000.0);
  const Stereo mono = render(engine, x, y, p);
  REQUIRE_FALSE(engine.rightRuns());
  REQUIRE(mono.l == mono.r);
  engine.setStereo(true);
  const Stereo same = render(engine, x, x, p);
  REQUIRE(engine.rightRuns());
  REQUIRE(same.l == same.r);
  const Stereo apart = render(engine, x, y, p);
  REQUIRE(apart.l != apart.r);
}

TEST_CASE("EQ survives NaN and processing does not allocate") {
  ShelvesEngine engine;
  engine.prepare(48000.0, 256);
  ParamValues bad;
  for (float& v : bad.p) v = std::numeric_limits<float>::quiet_NaN();
  const auto x = sine(480, 440.0, 48000.0);
  for (float s : render(engine, x, x, bad).l) REQUIRE(std::isfinite(s));
  ParamValues p = defaults();
  std::vector<float> l(16, 0.1f), ol(16), orr(16);
  const float* in[2] = {l.data(), l.data()};
  float* o[2] = {ol.data(), orr.data()};
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int block = 0; block < 300; ++block) {
      engine.setStereo(block % 100 < 50);
      p.p[1] = float(block % 10) / 10.0f;
      p.p[11] = float(block % 7) / 7.0f;
      engine.process(in, o, p, 16);
    }
    engine.reset();
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}

// Every gain at +18 or -18 dB, every Q at 40, every band at the low or high end of its range,
// with a loud saw-plus-noise input, at 44.1, 48 and 96 kHz, mono and stereo: the output stays
// finite and bounded (the circuit clips at about +-10.5 V; the down-sampling filter can ring
// past that briefly, measured at about 4x full scale).
TEST_CASE("EQ stays finite and bounded at extreme settings") {
  for (double rate : {44100.0, 48000.0, 96000.0}) {
    for (float gain : {0.0f, 1.0f}) {
      for (float freq : {0.0f, 1.0f}) {
        ParamValues p = test::defaultParams(ShelvesEngine::staticInfo());
        for (int band : {0, 2, 5, 8}) p.p[band] = freq;  // the four frequencies
        for (int g : {1, 3, 6, 9}) p.p[g] = gain;        // the four gains
        p.p[4] = 1.0f;                                   // Mid 1 Q 40
        p.p[7] = 1.0f;                                   // Mid 2 Q 40
        ShelvesEngine engine;
        engine.prepare(rate, 64);
        engine.setStereo(true);
        const size_t n = size_t(rate);  // one second
        std::vector<float> inL(n), inR(n), l(n), r(n);
        uint32_t seed = 1;
        for (size_t i = 0; i < n; ++i) {
          seed = seed * 1664525u + 1013904223u;
          const float noise = float(seed >> 8) / 8388608.0f - 1.0f;
          inL[i] = 0.9f * float(2.0 * std::fmod(55.0 * double(i) / rate, 1.0) - 1.0);
          inR[i] = noise;
        }
        for (size_t done = 0; done < n; done += 64) {
          const int k = int(std::min<size_t>(64, n - done));
          const float* in[2] = {inL.data() + done, inR.data() + done};
          float* out[2] = {l.data() + done, r.data() + done};
          engine.process(in, out, p, k);
        }
        float peak = 0.0f;
        bool finite = true;
        for (size_t i = 0; i < n; ++i) {
          finite = finite && std::isfinite(l[i]) && std::isfinite(r[i]);
          peak = std::max({peak, std::fabs(l[i]), std::fabs(r[i])});
        }
        INFO("rate " << rate << " gain " << gain << " freq " << freq << " peak " << peak);
        REQUIRE(finite);
        REQUIRE(peak > 0.1f);
        REQUIRE(peak < 8.0f);
      }
    }
  }
}
