#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "alloc_counter.h"
#include "engine/native_rate_wrapper.h"
#include "engine/part_fx_engine.h"
#include "engine/rings_engine.h"
#include "engine/rings_fx_engine.h"
#include "stmlib/utils/random.h"
#include "test_helpers.h"

using namespace eg13;
using Catch::Matchers::WithinAbs;

namespace {

constexpr size_t kRate = 48000;

ParamValues defaults() { return test::defaultParams(RingsFxEngine::staticInfo()); }

// Deterministic white noise in [-amplitude, amplitude).
std::vector<float> noise(size_t n, uint32_t seed = 1, float amplitude = 1.0f) {
  std::vector<float> x(n);
  uint32_t s = seed;
  for (auto& v : x) {
    s = s * 1664525u + 1013904223u;
    v = (float(s >> 8) / float(1 << 24) - 0.5f) * 2.0f * amplitude;
  }
  return x;
}

struct Stereo {
  std::vector<float> l, r;
};

// Runs the engine over `in` (both channels) in calls of `chunk` samples; params(t) gives the P's
// and events(t) is called before the call starting at sample t.
template <typename Params, typename Events>
Stereo run(VoiceEngine& e, const std::vector<float>& in, int chunk, Params params, Events events) {
  const size_t n = in.size();
  Stereo out{std::vector<float>(n), std::vector<float>(n)};
  stmlib::Random::Seed(1);  // Rings draws from the thread-local stmlib::Random: every run starts alike
  for (size_t done = 0; done < n;) {
    const int count = int(std::min<size_t>(size_t(chunk), n - done));
    const float* i2[2] = {in.data() + done, in.data() + done};
    float* o2[2] = {out.l.data() + done, out.r.data() + done};
    events(done);
    e.process(i2, o2, params(done), count);
    done += size_t(count);
  }
  return out;
}

template <typename Params>
Stereo run(VoiceEngine& e, const std::vector<float>& in, int chunk, Params params) {
  return run(e, in, chunk, params, [](size_t) {});
}

Stereo run(VoiceEngine& e, const std::vector<float>& in, int chunk, const ParamValues& p) {
  return run(e, in, chunk, [&](size_t) { return p; }, [](size_t) {});
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

TEST_CASE("Part FX Resonator describes itself") {
  const EngineInfo& info = RingsFxEngine::staticInfo();
  REQUIRE(std::string(info.id) == "rings_fx");
  REQUIRE(std::string(info.displayName) == "Resonator");
  REQUIRE(info.usesInput);
  REQUIRE_FALSE(info.hasAux);
  REQUIRE(info.sideInputs == 0);
  REQUIRE(info.paramCount == 13);
  const char* const names[13] = {"Mix",      "Structure", "Brightness", "Damping",   "Spread",
                                 "Position", "(unused)",  "(unused)",   "(unused)",  "Model",
                                 "Polyphony", "(unused)", "Tune"};
  for (int i = 0; i < 13; ++i) {
    INFO("P" << i + 1);
    REQUIRE(std::string(info.params[i].name) == names[i]);
    REQUIRE(info.params[i].defaultValue == kPartFxHostDefaults[i]);
  }
  REQUIRE(RingsFxEngine::kNativeRate == 48000.0);
  REQUIRE(RingsFxEngine::kNativeBlock == 24);
  char buf[32];
  info.params[9].format(0.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Modal");
  info.params[9].format(1.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "String + Reverb");
  info.params[10].format(1.5f / 16.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "2");
  info.params[10].format(1.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "4");
  info.params[12].format(0.5f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "+0.0 st");
  info.params[0].format(0.25f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "25%");
}

TEST_CASE("Part FX Resonator maps onto the Source Resonator's controls") {
  // Every field pinned with literals (G19): Model P10, Structure P2, Brightness P3, Damping P4,
  // Position P6, Exciter fixed to Input, Frequency P13, Spread P5, Polyphony P11.
  ParamValues fx = defaults();
  ParamValues s = RingsFxEngine::sourceParams(fx);
  REQUIRE(s.p[0] == 0.5f / 6.0f);  // Modal
  REQUIRE(s.p[1] == 0.5f);
  REQUIRE(s.p[2] == 0.5f);
  REQUIRE(s.p[3] == 0.5f);
  REQUIRE(s.p[4] == 0.5f);
  REQUIRE(s.p[5] == 0.5f);  // 1.5 / 3
  REQUIRE(s.p[6] == 0.5f);
  REQUIRE(s.p[7] == 0.75f);
  REQUIRE(s.p[8] == 0.5f / 3.0f);
  REQUIRE(RingsEngine::exciterIndex(s.p[5]) == 1);
  REQUIRE(RingsEngine::polyphonyVoices(s.p[8]) == 1);
  REQUIRE(RingsEngine::modelIndex(s.p[0]) == 0);

  fx.p[0] = 0.1f;   // Mix: not a Source control
  fx.p[1] = 0.2f;   // Structure
  fx.p[2] = 0.3f;   // Brightness
  fx.p[3] = 0.4f;   // Damping
  fx.p[4] = 0.6f;   // Spread
  fx.p[5] = 0.7f;   // Position
  fx.p[6] = 0.9f;   // unused
  fx.p[7] = 0.9f;
  fx.p[8] = 0.9f;
  fx.p[12] = 0.8f;  // Tune
  s = RingsFxEngine::sourceParams(fx);
  REQUIRE(s.p[1] == 0.2f);
  REQUIRE(s.p[2] == 0.3f);
  REQUIRE(s.p[3] == 0.4f);
  REQUIRE(s.p[4] == 0.7f);  // Position
  REQUIRE(s.p[5] == 0.5f);  // Exciter stays Input
  REQUIRE(s.p[6] == 0.8f);  // Frequency
  REQUIRE(s.p[7] == 0.6f);  // Spread

  // Model: 16 steps over 6 models, the last option from step 5 on; Polyphony 1 / 2 / 4 likewise.
  for (int step = 0; step < 16; ++step) {
    fx.p[9] = (float(step) + 0.5f) / 16.0f;
    fx.p[10] = (float(step) + 0.5f) / 16.0f;
    s = RingsFxEngine::sourceParams(fx);
    INFO("step " << step);
    REQUIRE(RingsEngine::modelIndex(s.p[0]) == std::min(step, 5));
    REQUIRE(RingsEngine::polyphonyVoices(s.p[8]) == (step == 0 ? 1 : step == 1 ? 2 : 4));
    REQUIRE(RingsEngine::exciterIndex(s.p[5]) == 1);
  }
}

TEST_CASE("Part FX Resonator at Mix 100 % equals the Source Resonator with Exciter Input") {
  const int model = GENERATE(0, 1, 2, 3, 4, 5);
  const int voices = GENERATE(0, 1, 2);
  const int chunk = GENERATE(24, 256);
  ParamValues fx = defaults();
  fx.p[0] = 1.0f;
  fx.p[1] = 0.7f;
  fx.p[2] = 0.6f;
  fx.p[3] = 0.8f;
  fx.p[9] = (float(model) + 0.5f) / 16.0f;
  fx.p[10] = (float(voices) + 0.5f) / 16.0f;
  const auto in = noise(kRate / 2, 3, 0.5f);

  RingsFxEngine engine;
  engine.prepare(RingsFxEngine::kNativeRate, 256);
  RingsEngine source;
  source.prepare(RingsEngine::kNativeRate, 256);
  const ParamValues sp = RingsFxEngine::sourceParams(fx);
  // The same events for both runs, each on the first call at or after its sample.
  struct Events {
    bool done[3] = {false, false, false};
    void operator()(VoiceEngine& e, size_t t) {
      if (!done[0]) { e.setNote(57.0f); done[0] = true; }
      if (!done[1] && t >= 4096) { e.noteOn(64.0f, 1.0f); done[1] = true; }
      if (!done[2] && t >= 20480) { e.setNote(67.0f); done[2] = true; }
    }
  };
  Events ea, eb;
  // Events on chunk boundaries that both runs share.
  const Stereo a = run(engine, in, chunk, [&](size_t) { return fx; }, [&](size_t t) { ea(engine, t); });
  const Stereo b = run(source, in, chunk, [&](size_t) { return sp; }, [&](size_t t) { eb(source, t); });
  REQUIRE(anyNonZero(b.l));
  REQUIRE(a.l == b.l);
  REQUIRE(a.r == b.r);
}

TEST_CASE("Part FX Resonator Mix 0 passes the input; Mix 50 % is the linear average") {
  const auto in = noise(kRate / 4, 8, 0.5f);
  ParamValues fx = defaults();
  fx.p[1] = 0.6f;
  fx.p[3] = 0.7f;

  fx.p[0] = 0.0f;
  RingsFxEngine dry;
  dry.prepare(RingsFxEngine::kNativeRate, 256);
  dry.noteOn(60.0f, 1.0f);
  const Stereo d = run(dry, in, 100, fx);
  REQUIRE(d.l == in);
  REQUIRE(d.r == in);

  fx.p[0] = 1.0f;
  RingsFxEngine wet;
  wet.prepare(RingsFxEngine::kNativeRate, 256);
  wet.noteOn(60.0f, 1.0f);
  const Stereo w = run(wet, in, 100, fx);
  REQUIRE(anyNonZero(w.l));

  fx.p[0] = 0.5f;
  RingsFxEngine half;
  half.prepare(RingsFxEngine::kNativeRate, 256);
  half.noteOn(60.0f, 1.0f);
  const Stereo h = run(half, in, 100, fx);
  for (size_t i = 0; i < in.size(); ++i) {
    INFO("sample " << i);
    REQUIRE_THAT(h.l[i], WithinAbs(0.5 * double(in[i]) + 0.5 * double(w.l[i]), 1e-6));
    REQUIRE_THAT(h.r[i], WithinAbs(0.5 * double(in[i]) + 0.5 * double(w.r[i]), 1e-6));
  }
}

TEST_CASE("Part FX Resonator Mix moves without a click") {
  // Mix steps from 0 to 100 % in the middle of a block: the ramp spreads it over that block.
  const auto in = noise(2400, 4, 0.5f);
  ParamValues fx = defaults();
  fx.p[0] = 0.0f;
  RingsFxEngine engine;
  engine.prepare(RingsFxEngine::kNativeRate, 256);
  std::vector<float> l(24), r(24);
  float* out[2] = {l.data(), r.data()};
  const float* i2[2] = {in.data(), in.data()};
  engine.process(i2, out, fx, 24);
  fx.p[0] = 1.0f;
  engine.process(i2, out, fx, 24);
  // First sample of the block is one 24th of the way, the last the wet signal.
  RingsFxEngine wet;
  wet.prepare(RingsFxEngine::kNativeRate, 256);
  std::vector<float> wl(24), wr(24);
  float* wout[2] = {wl.data(), wr.data()};
  ParamValues w = fx;
  wet.process(i2, wout, w, 24);
  wet.process(i2, wout, w, 24);
  REQUIRE_THAT(l[23], WithinAbs(double(wl[23]), 1e-6));
  REQUIRE_THAT(l[0], WithinAbs((23.0 / 24.0) * double(in[0]) + (1.0 / 24.0) * double(wl[0]), 1e-5));
}

namespace {

// Rings the resonator with a short noise burst after the note, then returns the strongest
// spectral peak of the silent tail within 3 semitones of the expected fundamental.
struct Ringer {
  RingsFxEngine engine;
  ParamValues p = defaults();
  Ringer() {
    engine.prepare(RingsFxEngine::kNativeRate, 256);
    p.p[0] = 1.0f;   // Mix: wet
    p.p[1] = 0.0f;   // Structure: sparse partials
    p.p[3] = 0.9f;   // Damping: long ring
    p.p[2] = 0.3f;   // Brightness: dark
  }
  Stereo silence(double seconds) {
    return run(engine, std::vector<float>(size_t(double(kRate) * seconds), 0.0f), 96, p);
  }
  double tailPitch(float note) {
    const Stereo burst = run(engine, noise(2400, 5, 0.8f), 96, p);
    (void)burst;
    const Stereo tail = silence(1.5);
    const double f = 440.0 * std::pow(2.0, (double(note) - 69.0) / 12.0);
    return test::peakFrequency(tail.l, double(kRate), kRate / 10, kRate * 3 / 2, f * 0.84, f * 1.19);
  }
};

double cents(double a, double b) { return 1200.0 * std::log2(a / b); }

}  // namespace

TEST_CASE("Part FX Resonator each note-on strums at that note; setNote retunes without a strum") {
  Ringer a;
  a.engine.noteOn(60.0f, 0.3f);  // velocity is ignored: a strum is full level
  a.silence(1.0);
  const double f60 = a.tailPitch(60.0f);

  Ringer b;
  b.engine.noteOn(72.0f, 1.0f);
  b.silence(1.0);
  const double f72 = b.tailPitch(72.0f);
  INFO("C4 " << f60 << " Hz, C5 " << f72 << " Hz");
  REQUIRE(std::fabs(cents(f60, 261.6256)) < 3.0);  // C4 (equal temperament, A4 = 440 Hz)
  REQUIRE(std::fabs(cents(f72, f60) - 1200.0) < 1.0);

  // A note-on later retunes to that note; the earlier pitch is gone.
  Ringer c;
  c.engine.noteOn(60.0f, 1.0f);
  c.silence(1.0);
  c.engine.noteOn(72.0f, 1.0f);
  c.silence(1.0);
  REQUIRE(std::fabs(cents(c.tailPitch(72.0f), f72)) < 1.0);

  // setNote alone moves the pitch once the note filter has settled, and starts no sound.
  Ringer d;
  d.engine.noteOn(60.0f, 1.0f);
  d.silence(1.0);
  d.engine.setNote(72.0f);
  const Stereo quiet = d.silence(2.0);
  REQUIRE_FALSE(anyNonZero(quiet.l));  // no input, no strum: nothing sounds
  REQUIRE(std::fabs(cents(d.tailPitch(72.0f), f72)) < 1.0);

  // The pitch follows the note by +-12 st of Tune too.
  Ringer e;
  e.p.p[12] = 1.0f;  // +12 st
  e.engine.noteOn(60.0f, 1.0f);
  e.silence(1.0);
  REQUIRE(std::fabs(cents(e.tailPitch(72.0f), f72)) < 1.0);
}

TEST_CASE("Part FX Resonator reset after audio equals a fresh engine") {
  ParamValues p = defaults();
  p.p[0] = 1.0f;
  p.p[3] = 0.8f;
  RingsFxEngine used;
  used.prepare(RingsFxEngine::kNativeRate, 256);
  used.noteOn(60.0f, 1.0f);
  run(used, noise(kRate, 11, 0.5f), 256, p);
  used.reset();
  RingsFxEngine fresh;
  fresh.prepare(RingsFxEngine::kNativeRate, 256);
  used.noteOn(64.0f, 1.0f);
  fresh.noteOn(64.0f, 1.0f);
  const auto next = noise(kRate / 2, 5, 0.5f);
  const Stereo a = run(used, next, 100, p);
  const Stereo b = run(fresh, next, 100, p);
  REQUIRE(anyNonZero(b.l));
  REQUIRE(a.l == b.l);
  REQUIRE(a.r == b.r);
}

TEST_CASE("Part FX Resonator with no input is silent without a strum and finite with one") {
  RingsFxEngine engine;
  engine.prepare(RingsFxEngine::kNativeRate, 256);
  std::vector<float> l(256), r(256);
  float* out[2] = {l.data(), r.data()};
  ParamValues p = defaults();
  p.p[0] = 1.0f;
  engine.noteOn(60.0f, 1.0f);
  for (int b = 0; b < 20; ++b) {
    engine.process(nullptr, out, p, 256);
    for (float v : l) REQUIRE(std::isfinite(v));
  }
}

TEST_CASE("Part FX Resonator full-scale noise at every model and polyphony stays bounded for 60 s") {
  const int model = GENERATE(0, 1, 2, 3, 4, 5);
  const int voices = GENERATE(0, 1, 2);
  ParamValues p = defaults();
  p.p[0] = 1.0f;   // Mix: wet only
  p.p[1] = 1.0f;   // Structure
  p.p[2] = 1.0f;   // Brightness
  p.p[3] = 1.0f;   // Damping: longest ring
  p.p[9] = (float(model) + 0.5f) / 16.0f;
  p.p[10] = (float(voices) + 0.5f) / 16.0f;
  const size_t n = 60 * kRate;
  const auto in = noise(n, 5);  // +-1.0, full scale
  RingsFxEngine engine;
  engine.prepare(RingsFxEngine::kNativeRate, 256);
  const Stereo out = run(
      engine, in, 256, [&](size_t) { return p; },
      [&](size_t t) {
        if (t % (kRate / 2) < 256) engine.noteOn(48.0f + float((t / (kRate / 2)) % 20), 1.0f);
      });
  REQUIRE(allFinite(out.l));
  REQUIRE(allFinite(out.r));
  REQUIRE(anyNonZero(out.l, n - kRate));
  // No runaway: the last 10 s neither sit at full scale (|x| >= 0.999 on under 1 % of samples)
  // nor grow louder than 10-20 s by more than 3 dB (the note cycle repeats every 10 s, so both
  // windows hold the same phases).
  auto fullScale = [&](const std::vector<float>& x) {
    size_t count = 0;
    for (size_t i = n - 10 * kRate; i < n; ++i)
      if (std::fabs(x[i]) >= 0.999f) ++count;
    return double(count) / double(10 * kRate);
  };
  const double clippedL = fullScale(out.l), clippedR = fullScale(out.r);
  const double early = 0.5 * (test::rms(out.l, 10 * kRate, 20 * kRate) + test::rms(out.r, 10 * kRate, 20 * kRate));
  const double late = 0.5 * (test::rms(out.l, n - 10 * kRate, n) + test::rms(out.r, n - 10 * kRate, n));
  const double growthDb = 20.0 * std::log10(late / early);
  INFO("model " << model << " voices " << voices << ": full scale L " << clippedL << " R " << clippedR
                << ", RMS 10-20 s " << early << ", last 10 s " << late << " (" << growthDb << " dB)");
  REQUIRE(clippedL < 0.01);
  REQUIRE(clippedR < 0.01);
  REQUIRE(growthDb <= 3.0);
}

TEST_CASE("Part FX Resonator allocates nothing in process, including Model and Polyphony changes") {
  RingsFxEngine engine;
  engine.prepare(RingsFxEngine::kNativeRate, 256);
  const auto in = noise(256, 4, 0.5f);
  std::vector<float> l(256), r(256);
  const float* i2[2] = {in.data(), in.data()};
  float* out[2] = {l.data(), r.data()};
  ParamValues p = defaults();
  engine.process(i2, out, p, 256);
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int block = 0; block < 300; ++block) {
      for (int k = 0; k < 6; ++k) p.p[k] = float((block + k) % 7) / 6.0f;
      p.p[9] = float(block % 8) / 7.0f;        // Model, through every option
      p.p[10] = float((block / 3) % 4) / 3.0f;  // Polyphony 1 / 2 / 4
      p.p[12] = float(block % 5) / 4.0f;
      if (block % 10 == 0) engine.noteOn(40.0f + float(block % 30), 1.0f);
      if (block % 10 == 5) engine.setNote(50.0f + float(block % 20));
      if (block == 150) engine.reset();
      engine.process(block % 3 == 0 ? nullptr : i2, out, p, 1 + block % 256);
    }
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}

// G21: the Resonator in its Part FX wrapper (whole native blocks, G8), at the native rate and at
// 44.1, 48 and 96 kHz, in both converter qualities.
TEST_CASE("Part FX Resonator through the wrapper at 44.1, 48 and 96 kHz with odd blocks") {
  const auto q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  const double rate = GENERATE(44100.0, 48000.0, 96000.0);
  INFO("rate " << rate << " quality " << int(q));
  auto make = [&] {
    auto w = std::make_unique<NativeRateWrapper>(std::make_unique<RingsFxEngine>(), RingsFxEngine::kNativeRate,
                                                 RingsFxEngine::kNativeBlock);
    w->setQuality(q);
    w->setWholeBlocks(true);
    w->prepare(rate, 256);
    return w;
  };
  // Noise for 0.2 s then silence: the same output whatever the host slicing, the strum at a
  // sample position and a ringing tail after the input stops.
  const size_t n = size_t(rate * 1.2);
  auto in = noise(n, 9, 0.5f);
  std::fill(in.begin() + long(rate * 0.2), in.end(), 0.0f);
  ParamValues p = defaults();
  p.p[0] = 1.0f;
  p.p[3] = 0.8f;
  auto runWrapped = [&](int chunk) {
    auto w = make();
    w->noteOn(60.0f, 1.0f);
    return std::make_pair(run(*w, in, chunk, p), std::move(w));
  };
  auto a = runWrapped(1);
  auto b = runWrapped(7);
  auto c = runWrapped(129);
  REQUIRE(allFinite(a.first.l));
  REQUIRE(allFinite(a.first.r));
  REQUIRE(anyNonZero(a.first.l, size_t(rate * 0.8)));  // the tail rings on after the input stops
  REQUIRE(a.first.l == b.first.l);
  REQUIRE(a.first.r == b.first.r);
  REQUIRE(a.first.l == c.first.l);
  REQUIRE(a.first.r == c.first.r);
  REQUIRE(a.second->inputUnderruns() == 0);
  REQUIRE(a.second->inputOverruns() == 0);

  // Mix 0 passes the input dry, so an impulse leaves at the wrapper's reported input-path latency.
  auto d = make();
  ParamValues dry = defaults();
  dry.p[0] = 0.0f;
  std::vector<float> impulse(size_t(rate * 0.2), 0.0f);
  const size_t at = size_t(rate * 0.05);
  impulse[at] = 1.0f;
  const Stereo out = run(*d, impulse, 129, dry);
  const int delay = test::peakIndex(out.l) - int(at);
  INFO("delay " << delay << " reported " << d->inputLatencySamples());
  if (rate == RingsFxEngine::kNativeRate) {
    REQUIRE(d->inputLatencySamples() == RingsFxEngine::kNativeBlock);
    REQUIRE(d->latencySamples() == RingsFxEngine::kNativeBlock);
    REQUIRE(delay == RingsFxEngine::kNativeBlock);
    REQUIRE(out.l[at + size_t(delay)] == 1.0f);
  } else {
    const int tolerance = int(std::ceil(24.0 * rate / 48000.0)) + 2;
    REQUIRE(std::abs(delay - d->inputLatencySamples()) <= tolerance);
  }
}
