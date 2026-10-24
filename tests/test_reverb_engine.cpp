#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "alloc_counter.h"
#include "elements/dsp/fx/reverb.h"
#include "engine/native_rate_wrapper.h"
#include "engine/part_fx_engine.h"
#include "engine/reverb_engine.h"
#include "test_helpers.h"

using namespace eg13;
using Catch::Matchers::WithinAbs;

namespace {

ParamValues defaults() { return test::defaultParams(ReverbEngine::staticInfo()); }

// Deterministic white noise in [-0.5, 0.5).
std::vector<float> noise(size_t n, uint32_t seed = 1) {
  std::vector<float> x(n);
  uint32_t s = seed;
  for (auto& v : x) {
    s = s * 1664525u + 1013904223u;
    v = float(s >> 8) / float(1 << 24) - 0.5f;
  }
  return x;
}

struct Stereo {
  std::vector<float> l, r;
};

// Runs the engine over `in` (both channels) in calls of `chunk` samples; params(t) gives the P's
// for the call starting at sample t.
template <typename Params>
Stereo run(VoiceEngine& e, const std::vector<float>& in, int chunk, Params params) {
  const size_t n = in.size();
  Stereo out{std::vector<float>(n), std::vector<float>(n)};
  for (size_t done = 0; done < n;) {
    const int count = int(std::min<size_t>(size_t(chunk), n - done));
    const float* i2[2] = {in.data() + done, in.data() + done};
    float* o2[2] = {out.l.data() + done, out.r.data() + done};
    e.process(i2, o2, params(done), count);
    done += size_t(count);
  }
  return out;
}

Stereo run(VoiceEngine& e, const std::vector<float>& in, int chunk, const ParamValues& p) {
  return run(e, in, chunk, [&](size_t) { return p; });
}

// The bare Elements reverb as elements::Part drives it (elements/dsp/part.cc:237-249): every
// 16-sample block, the setters, then Process. The laws are restated here, not taken from the
// adapter: Amount P1, Time 0.35 + 0.63 P2, Diffusion 0.55 + 0.15 P3, Brightness 0.6 + 0.37 P4,
// input gain 0.2; Freeze: time 1, input gain 0, lp 1. Smoothing: Elements' pot filter,
// pot += 0.01 * (target - pot) once per block (elements/cv_scaler.cc:149), snapped at the start.
class Reference {
 public:
  Reference() : buffer_(32768) {
    std::memset(static_cast<void*>(&reverb_), 0, sizeof(reverb_));  // Init leaves members unset (G5)
    reverb_.Init(buffer_.data());
  }

  void block(const float* inL, const float* inR, float* outL, float* outR, const ParamValues& p) {
    for (int k = 0; k < 4; ++k) {
      const float target = p.p[k];
      pot_[k] = first_ ? target : pot_[k] + 0.01f * (target - pot_[k]);
    }
    first_ = false;
    const bool freeze = p.p[8] >= 0.5f;
    float l[16], r[16];
    for (int i = 0; i < 16; ++i) {
      l[i] = inL[i];
      r[i] = inR[i];
    }
    reverb_.set_amount(pot_[0]);
    reverb_.set_diffusion(0.55f + 0.15f * pot_[2]);
    if (freeze) {
      reverb_.set_time(1.0f);
      reverb_.set_input_gain(0.0f);
      reverb_.set_lp(1.0f);
    } else {
      reverb_.set_time(0.35f + 0.63f * pot_[1]);
      reverb_.set_input_gain(0.2f);
      reverb_.set_lp(0.6f + 0.37f * pot_[3]);
    }
    reverb_.Process(l, r, 16);
    for (int i = 0; i < 16; ++i) {
      outL[i] = l[i];
      outR[i] = r[i];
    }
  }

  template <typename Params>
  Stereo run(const std::vector<float>& in, Params params) {
    const size_t n = in.size();
    Stereo out{std::vector<float>(n), std::vector<float>(n)};
    for (size_t t = 0; t + 16 <= n; t += 16) {
      block(in.data() + t, in.data() + t, out.l.data() + t, out.r.data() + t, params(t));
    }
    return out;
  }

 private:
  elements::Reverb reverb_;
  std::vector<uint16_t> buffer_;
  float pot_[4] = {};
  bool first_ = true;
};

double energy(const std::vector<float>& x, size_t begin, size_t end) {
  double sum = 0.0;
  for (size_t i = begin; i < end; ++i) sum += double(x[i]) * double(x[i]);
  return sum / double(end - begin);
}

bool allFinite(const std::vector<float>& x) {
  for (float v : x)
    if (!std::isfinite(v)) return false;
  return true;
}

bool anyNonZero(const std::vector<float>& x, size_t begin = 0) {
  for (size_t i = begin; i < x.size(); ++i)
    if (x[i] != 0.0f) return true;
  return false;
}

}  // namespace

TEST_CASE("Part FX stepped options and switches") {
  REQUIRE(kPartFxParamCount == 16);
  REQUIRE(kPartFxSelectorSteps == 16);
  // 16 fixed steps; steps past the last option read as the last option.
  REQUIRE(partFxOption(0.0f, 3) == 0);
  REQUIRE(partFxOption(0.99f / 16.0f, 3) == 0);
  REQUIRE(partFxOption(1.0f / 16.0f, 3) == 1);
  REQUIRE(partFxOption(2.5f / 16.0f, 3) == 2);
  REQUIRE(partFxOption(3.5f / 16.0f, 3) == 2);
  REQUIRE(partFxOption(1.0f, 3) == 2);
  REQUIRE(partFxOption(1.0f, 16) == 15);
  REQUIRE(partFxOption(std::nanf(""), 3) == 0);
  // Switches are on at 0.5 or more.
  REQUIRE_FALSE(partFxSwitch(0.0f));
  REQUIRE_FALSE(partFxSwitch(0.4999f));
  REQUIRE(partFxSwitch(0.5f));
  REQUIRE(partFxSwitch(1.0f));
  REQUIRE_FALSE(partFxSwitch(std::nanf("")));
}

TEST_CASE("Reverb describes itself") {
  const EngineInfo& info = ReverbEngine::staticInfo();
  REQUIRE(std::string(info.id) == "elements_reverb");
  REQUIRE(std::string(info.displayName) == "Reverb");
  REQUIRE(info.usesInput);
  REQUIRE_FALSE(info.hasAux);
  REQUIRE(info.sideInputs == 0);
  REQUIRE(info.paramCount == 9);
  const char* const names[9] = {"Amount", "Time", "Diffusion", "Brightness", "(unused)",
                                "(unused)", "(unused)", "(unused)", "Freeze"};
  for (int i = 0; i < 9; ++i) {
    INFO("P" << i + 1);
    REQUIRE(std::string(info.params[i].name) == names[i]);
    REQUIRE(info.params[i].defaultValue == kPartFxHostDefaults[i]);
  }
  REQUIRE(ReverbEngine::kNativeRate == 32000.0);
  REQUIRE(ReverbEngine::kNativeBlock == 16);
  char buf[32];
  info.params[8].format(0.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Off");
  info.params[8].format(0.5f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "On");
  info.params[0].format(0.25f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "25%");
}

TEST_CASE("Reverb mappings follow Elements and Clouds") {
  REQUIRE(ReverbEngine::timeFor(0.0f) == 0.35f);
  REQUIRE_THAT(ReverbEngine::timeFor(1.0f), WithinAbs(0.98f, 1e-6));
  REQUIRE(ReverbEngine::diffusionFor(0.5f) == 0.625f);
  REQUIRE_THAT(ReverbEngine::diffusionFor(0.0f), WithinAbs(0.55f, 1e-6));
  REQUIRE_THAT(ReverbEngine::diffusionFor(1.0f), WithinAbs(0.70f, 1e-6));
  REQUIRE_THAT(ReverbEngine::lpFor(0.0f), WithinAbs(0.6f, 1e-6));
  REQUIRE_THAT(ReverbEngine::lpFor(1.0f), WithinAbs(0.97f, 1e-6));
}

TEST_CASE("Reverb matches the bare Elements reverb at 32 kHz") {
  // Steady P's, so the adapter's smoother holds them exactly (it snaps on the first block). The
  // values make each law's product exact, so a fused multiply-add cannot round differently.
  ParamValues other = defaults();
  other.p[0] = 0.25f;
  other.p[1] = 1.0f;
  other.p[2] = 0.25f;
  other.p[3] = 0.0f;
  ParamValues frozen = defaults();
  frozen.p[8] = 1.0f;
  const ParamValues sets[3] = {defaults(), other, frozen};
  const int set = GENERATE(0, 1, 2);
  const ParamValues p = sets[set];
  const auto in = noise(32000 * 5);  // 1 s to settle, then 4 s compared
  ReverbEngine engine;
  engine.prepare(ReverbEngine::kNativeRate, 256);
  Reference reference;
  const Stereo out = run(engine, in, 256, p);
  const Stereo ref = reference.run(in, [&](size_t) { return p; });
  INFO("set " << set);
  REQUIRE(anyNonZero(ref.l, 32000));
  REQUIRE(out.l == ref.l);
  REQUIRE(out.r == ref.r);
}

TEST_CASE("Reverb follows moving controls like Elements' pot filter") {
  // Every control steps several times; Freeze toggles. Bit for bit against the bare reverb with
  // the firmware's per-block pot filter.
  auto params = [](size_t t) {
    ParamValues p = test::defaultParams(ReverbEngine::staticInfo());
    const size_t phase = t / 8000;
    p.p[0] = float(phase % 5) / 4.0f;
    p.p[1] = float((phase + 2) % 3) / 2.0f;
    p.p[2] = float(phase % 4) / 3.0f;
    p.p[3] = float((phase + 1) % 5) / 4.0f;
    p.p[8] = phase % 7 == 3 ? 1.0f : 0.0f;
    return p;
  };
  const auto in = noise(32000 * 3, 7);
  ReverbEngine engine;
  engine.prepare(ReverbEngine::kNativeRate, 256);
  Reference reference;
  const Stereo out = run(engine, in, 16, params);
  const Stereo ref = reference.run(in, params);
  REQUIRE(out.l == ref.l);
  REQUIRE(out.r == ref.r);
}

TEST_CASE("Reverb Freeze holds the tail and ignores new input") {
  ParamValues p = defaults();
  p.p[0] = 1.0f;  // Amount: wet only
  p.p[1] = 1.0f;  // Time
  ParamValues frozen = p;
  frozen.p[8] = 1.0f;
  const size_t rate = 32000;
  const size_t freezeAt = 2 * rate;
  const size_t n = freezeAt + 5 * rate;
  // The input keeps playing after Freeze, louder; a second run gets silence from there on.
  auto in = noise(n, 3);
  for (size_t i = freezeAt; i < n; ++i) in[i] *= 2.0f;
  auto quiet = in;
  std::fill(quiet.begin() + long(freezeAt), quiet.end(), 0.0f);
  auto params = [&](size_t t) { return t < freezeAt ? p : frozen; };
  ReverbEngine a, b;
  a.prepare(ReverbEngine::kNativeRate, 256);
  b.prepare(ReverbEngine::kNativeRate, 256);
  const Stereo withInput = run(a, in, 16, params);
  const Stereo silent = run(b, quiet, 16, params);

  const double atFreeze = energy(silent.l, freezeAt, freezeAt + rate / 10);
  INFO("energy at freeze " << atFreeze);
  REQUIRE(atFreeze > 1e-6);
  for (size_t w = freezeAt + rate / 10; w + rate / 2 <= n; w += rate / 2) {
    const double e = energy(silent.l, w, w + rate / 2);
    INFO("window at " << double(w - freezeAt) / double(rate) << " s: " << e);
    REQUIRE(e > 0.5 * atFreeze);
  }
  // New input adds nothing: with Amount at 100 % the output is the wet signal (to float
  // rounding of Elements' crossfade, x + (wet - x)).
  double worst = 0.0;
  for (size_t i = freezeAt; i < n; ++i) {
    worst = std::max(worst, double(std::fabs(withInput.l[i] - silent.l[i])));
    worst = std::max(worst, double(std::fabs(withInput.r[i] - silent.r[i])));
  }
  INFO("largest difference " << worst);
  REQUIRE(worst < 1e-5);
}

TEST_CASE("Reverb smoothing: gradual steps, convergence, snap after reset") {
  // Elements' pot filter, 0.01 once per 16-sample block (elements/cv_scaler.cc:149), on
  // Amount, Time, Diffusion and Brightness. Freeze switches at once.
  ReverbEngine engine;
  engine.prepare(ReverbEngine::kNativeRate, 256);
  std::vector<float> l(16), r(16);
  float* out[2] = {l.data(), r.data()};
  ParamValues p = defaults();
  p.p[0] = 0.0f;
  p.p[1] = 0.0f;
  engine.process(nullptr, out, p, 16);
  REQUIRE(engine.smoothedParam(0) == 0.0f);  // the first block after reset snaps
  REQUIRE(engine.smoothedParam(2) == 0.5f);

  ParamValues step = p;
  step.p[0] = 1.0f;  // Amount 0 -> 1
  step.p[1] = 1.0f;  // Time 0 -> 1
  step.p[2] = 0.0f;  // Diffusion 0.5 -> 0
  step.p[3] = 1.0f;  // Brightness 0.5 -> 1
  step.p[8] = 1.0f;  // Freeze: a switch
  engine.process(nullptr, out, step, 16);
  REQUIRE_THAT(engine.smoothedParam(0), WithinAbs(0.01, 1e-6));
  REQUIRE_THAT(engine.smoothedParam(1), WithinAbs(0.01, 1e-6));
  REQUIRE_THAT(engine.smoothedParam(2), WithinAbs(0.495, 1e-6));
  REQUIRE_THAT(engine.smoothedParam(3), WithinAbs(0.505, 1e-6));
  REQUIRE(engine.smoothedParam(8) == 1.0f);

  float previous = engine.smoothedParam(0);
  for (int block = 2; block <= 2000; ++block) {
    engine.process(nullptr, out, step, 16);
    const float v = engine.smoothedParam(0);
    INFO("block " << block << " value " << v);
    REQUIRE(v >= previous);
    REQUIRE(v - previous <= 0.01f);
    if (block == 100) REQUIRE_THAT(v, WithinAbs(1.0 - std::pow(0.99, 100), 1e-4));
    previous = v;
  }
  REQUIRE(previous > 0.9999f);

  // A partial block advances the filter by its share of a native block.
  ReverbEngine c;
  c.prepare(ReverbEngine::kNativeRate, 256);
  c.process(nullptr, out, p, 16);
  c.process(nullptr, out, step, 8);
  c.process(nullptr, out, step, 8);
  REQUIRE_THAT(c.smoothedParam(0), WithinAbs(0.01, 1e-6));

  // reset() snaps again on the next block.
  engine.reset();
  engine.process(nullptr, out, p, 16);
  REQUIRE(engine.smoothedParam(0) == 0.0f);
  REQUIRE(engine.smoothedParam(2) == 0.5f);
}

TEST_CASE("Reverb reset after 2 s of audio equals a fresh engine") {
  ParamValues p = defaults();
  p.p[1] = 0.9f;
  const auto in = noise(32000 * 2, 11);
  ReverbEngine used;
  used.prepare(ReverbEngine::kNativeRate, 256);
  run(used, in, 256, p);
  used.reset();
  ReverbEngine fresh;
  fresh.prepare(ReverbEngine::kNativeRate, 256);
  const auto next = noise(32000, 5);
  const Stereo a = run(used, next, 256, p);
  const Stereo b = run(fresh, next, 256, p);
  REQUIRE(a.l == b.l);
  REQUIRE(a.r == b.r);
}

TEST_CASE("Reverb ignores notes") {
  const auto in = noise(16000, 2);
  ReverbEngine a, b;
  a.prepare(ReverbEngine::kNativeRate, 256);
  b.prepare(ReverbEngine::kNativeRate, 256);
  a.noteOn(60.0f, 1.0f);
  a.setNote(72.0f);
  a.noteOff();
  const Stereo x = run(a, in, 256, defaults());
  const Stereo y = run(b, in, 256, defaults());
  REQUIRE(x.l == y.l);
  REQUIRE(x.r == y.r);
}

TEST_CASE("Reverb with no input decays to silence") {
  ReverbEngine engine;
  engine.prepare(ReverbEngine::kNativeRate, 256);
  std::vector<float> l(256), r(256);
  float* out[2] = {l.data(), r.data()};
  // Feed it first (1 s of noise), so the silence afterwards is a decay and not an empty start.
  const auto in = noise(256, 4);
  const float* i2[2] = {in.data(), in.data()};
  for (int b = 0; b < 188; ++b) engine.process(i2, out, defaults(), 256);
  REQUIRE(test::rms(l, 0, l.size()) > 1e-4);  // it rings
  for (int b = 0; b < 2000; ++b) engine.process(nullptr, out, defaults(), 256);
  for (float v : l) REQUIRE(v == 0.0f);
}

TEST_CASE("Reverb allocates nothing in process") {
  ReverbEngine engine;
  engine.prepare(ReverbEngine::kNativeRate, 256);
  const auto in = noise(256, 4);
  std::vector<float> l(256), r(256);
  const float* i2[2] = {in.data(), in.data()};
  float* out[2] = {l.data(), r.data()};
  ParamValues p = defaults();
  engine.process(i2, out, p, 256);
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int block = 0; block < 200; ++block) {
      for (int k = 0; k < 4; ++k) p.p[k] = float((block + k) % 7) / 6.0f;
      p.p[8] = block % 20 < 10 ? 0.0f : 1.0f;
      if (block == 100) engine.reset();
      engine.process(block % 3 == 0 ? nullptr : i2, out, p, 1 + block % 256);
    }
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}

// G21: the Reverb in its Part FX wrapper (whole native blocks, G8), at the native rate and at
// 44.1, 48 and 96 kHz, in both converter qualities.
TEST_CASE("Reverb through the wrapper at 32, 44.1, 48 and 96 kHz with odd blocks") {
  const auto q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  const double rate = GENERATE(32000.0, 44100.0, 48000.0, 96000.0);
  INFO("rate " << rate << " quality " << int(q));
  auto make = [&] {
    auto w = std::make_unique<NativeRateWrapper>(std::make_unique<ReverbEngine>(), ReverbEngine::kNativeRate,
                                                 ReverbEngine::kNativeBlock);
    w->setQuality(q);
    w->setWholeBlocks(true);
    w->prepare(rate, 256);
    return w;
  };
  // Noise for half a second, then the tail: the same output whatever the host slicing.
  const size_t n = size_t(rate * 1.5);
  auto in = noise(n, 9);
  std::fill(in.begin() + long(rate * 0.5), in.end(), 0.0f);
  ParamValues p = defaults();
  p.p[1] = 0.8f;
  auto a = make();
  auto b = make();
  auto c = make();
  const Stereo ra = run(*a, in, 1, p);
  const Stereo rb = run(*b, in, 7, p);
  const Stereo rc = run(*c, in, 129, p);
  REQUIRE(allFinite(ra.l));
  REQUIRE(allFinite(ra.r));
  REQUIRE(anyNonZero(ra.l, size_t(rate)));  // the tail rings on after the input stops
  REQUIRE(ra.l == rb.l);
  REQUIRE(ra.r == rb.r);
  REQUIRE(ra.l == rc.l);
  REQUIRE(ra.r == rc.r);
  REQUIRE(a->inputUnderruns() == 0);
  REQUIRE(a->inputOverruns() == 0);

  // Amount 0 passes the input dry (Elements' crossfade at 0), so an impulse leaves at the
  // wrapper's reported input-path latency.
  auto d = make();
  ParamValues dry = defaults();
  dry.p[0] = 0.0f;
  std::vector<float> impulse(size_t(rate * 0.2), 0.0f);
  const size_t at = size_t(rate * 0.05);
  impulse[at] = 1.0f;
  const Stereo out = run(*d, impulse, 129, dry);
  const int delay = test::peakIndex(out.l) - int(at);
  INFO("delay " << delay << " reported " << d->inputLatencySamples());
  if (rate == ReverbEngine::kNativeRate) {
    REQUIRE(d->inputLatencySamples() == ReverbEngine::kNativeBlock);
    REQUIRE(d->latencySamples() == ReverbEngine::kNativeBlock);
    REQUIRE(delay == ReverbEngine::kNativeBlock);
    REQUIRE(out.l[at + size_t(delay)] == 1.0f);
  } else {
    const int tolerance = int(std::ceil(16.0 * rate / 32000.0)) + 2;
    REQUIRE(std::abs(delay - d->inputLatencySamples()) <= tolerance);
  }
}
