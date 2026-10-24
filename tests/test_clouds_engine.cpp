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
#include "clouds/dsp/granular_processor.h"
#include "engine/clouds_engine.h"
#include "engine/native_rate_wrapper.h"
#include "engine/part_fx_engine.h"
#include "stmlib/utils/random.h"
#include "test_helpers.h"

using namespace eg13;
using Catch::Matchers::WithinAbs;

namespace {

ParamValues defaults() { return test::defaultParams(CloudsEngine::staticInfo()); }

// The middle of option k's step of a stepped P (16 fixed steps).
float option(int k) { return (float(k) + 0.5f) / 16.0f; }

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

std::vector<float> sine(size_t n, double hz, double rate, float amplitude) {
  std::vector<float> x(n);
  for (size_t i = 0; i < n; ++i)
    x[i] = amplitude * float(std::sin(2.0 * 3.14159265358979323846 * hz * double(i) / rate));
  return x;
}

struct Stereo {
  std::vector<float> l, r;
};

// Runs the engine over `in` (both channels) in calls of `chunk` samples; before the call starting
// at sample t, params(t) gives the P's and events(t) may send notes.
template <typename Params, typename Events>
Stereo run(VoiceEngine& e, const std::vector<float>& in, int chunk, Params params, Events events) {
  const size_t n = in.size();
  Stereo out{std::vector<float>(n), std::vector<float>(n)};
  for (size_t done = 0; done < n;) {
    const int count = int(std::min<size_t>(size_t(chunk), n - done));
    events(done);
    const float* i2[2] = {in.data() + done, in.data() + done};
    float* o2[2] = {out.l.data() + done, out.r.data() + done};
    e.process(i2, o2, params(done), count);
    done += size_t(count);
  }
  return out;
}

Stereo run(VoiceEngine& e, const std::vector<float>& in, int chunk, const ParamValues& p) {
  return run(
      e, in, chunk, [&](size_t) { return p; }, [](size_t) {});
}

// Bare Clouds as VCV drives it (AudibleInstruments/src/Clouds.cpp:103-104 and the loop at
// :139-197), at 32 kHz with no converters: every 32 samples the input becomes int16 as
// clamp(x * gain * 32767), then set_playback_mode, set_quality, Prepare, the parameters, Process,
// and the output / 32768. The laws are restated here, not taken from the adapter (preflight
// ruling G19): In Gain 2P, Pitch (2P - 1) * 24 semitones; Blend -> dry_wet, Spread ->
// stereo_spread, Freeze on at 50 %; a trigger sets trigger and gate for one block.
class Reference {
 public:
  Reference() : mem_(118784), ccm_(65536 - 128), processor_(std::make_unique<clouds::GranularProcessor>()) {
    std::memset(static_cast<void*>(processor_.get()), 0, sizeof(*processor_));
    processor_->Init(mem_.data(), mem_.size(), ccm_.data(), ccm_.size());
  }

  void block(const float* inL, const float* inR, float* outL, float* outR, const ParamValues& p, bool triggered) {
    const float gain = 2.0f * p.p[12];
    clouds::ShortFrame input[32];
    for (int i = 0; i < 32; ++i) {
      input[i].l = short(std::clamp(inL[i] * gain * 32767.0f, -32768.0f, 32767.0f));
      input[i].r = short(std::clamp(inR[i] * gain * 32767.0f, -32768.0f, 32767.0f));
    }
    const int mode = std::min(int(p.p[9] * 16.0f), 3);
    const int quality = std::min(int(p.p[10] * 16.0f), 3);
    processor_->set_playback_mode(clouds::PlaybackMode(mode));
    processor_->set_quality(quality);
    processor_->Prepare();
    clouds::Parameters* q = processor_->mutable_parameters();
    q->trigger = triggered;
    q->gate = triggered;
    q->freeze = p.p[8] >= 0.5f;
    q->position = p.p[1];
    q->size = p.p[2];
    q->pitch = std::clamp((2.0f * p.p[3] - 1.0f) * 24.0f, -48.0f, 48.0f);
    q->density = p.p[4];
    q->texture = p.p[5];
    q->dry_wet = p.p[0];
    q->stereo_spread = p.p[11];
    q->feedback = p.p[6];
    q->reverb = p.p[7];
    clouds::ShortFrame output[32];
    processor_->Process(input, output, 32);
    for (int i = 0; i < 32; ++i) {
      outL[i] = float(output[i].l / 32768.0);
      outR[i] = float(output[i].r / 32768.0);
    }
  }

  // trigger(t): a trigger for the block starting at sample t.
  template <typename Params, typename Trigger>
  Stereo run(const std::vector<float>& in, Params params, Trigger trigger) {
    const size_t n = in.size();
    Stereo out{std::vector<float>(n), std::vector<float>(n)};
    for (size_t t = 0; t + 32 <= n; t += 32) {
      block(in.data() + t, in.data() + t, out.l.data() + t, out.r.data() + t, params(t), trigger(t));
    }
    return out;
  }

 private:
  std::vector<uint8_t> mem_;
  std::vector<uint8_t> ccm_;
  std::unique_ptr<clouds::GranularProcessor> processor_;
};

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

double maxAbs(const std::vector<float>& x, size_t begin, size_t end) {
  double m = 0.0;
  for (size_t i = begin; i < end; ++i) m = std::max(m, double(std::fabs(x[i])));
  return m;
}

double maxStep(const std::vector<float>& x, size_t begin, size_t end) {
  double m = 0.0;
  for (size_t i = begin + 1; i < end; ++i) m = std::max(m, double(std::fabs(x[i] - x[i - 1])));
  return m;
}

}  // namespace

TEST_CASE("Granular describes itself") {
  const EngineInfo& info = CloudsEngine::staticInfo();
  REQUIRE(std::string(info.id) == "clouds");
  REQUIRE(std::string(info.displayName) == "Granular");
  REQUIRE(info.usesInput);
  REQUIRE_FALSE(info.hasAux);
  REQUIRE(info.sideInputs == 0);
  REQUIRE(info.paramCount == 13);
  const char* const names[13] = {"Blend",    "Position", "Size", "Pitch",   "Density", "Texture", "Feedback",
                                 "Reverb",   "Freeze",   "Mode", "Quality", "Spread",  "In Gain"};
  for (int i = 0; i < 13; ++i) {
    INFO("P" << i + 1);
    REQUIRE(std::string(info.params[i].name) == names[i]);
    REQUIRE(info.params[i].defaultValue == kPartFxHostDefaults[i]);
    REQUIRE(info.params[i].format != nullptr);
  }
  REQUIRE(info.params[4].defaultValue == 0.75f);
  REQUIRE(CloudsEngine::kNativeRate == 32000.0);
  REQUIRE(CloudsEngine::kNativeBlock == 32);
  REQUIRE(CloudsEngine::kNumModes == 4);
  REQUIRE(CloudsEngine::kNumQualities == 4);
  REQUIRE(CloudsEngine::pitchSemitones(0.0f) == -24.0f);
  REQUIRE(CloudsEngine::pitchSemitones(0.5f) == 0.0f);
  REQUIRE(CloudsEngine::pitchSemitones(1.0f) == 24.0f);
  REQUIRE(CloudsEngine::inputGain(0.0f) == 0.0f);
  REQUIRE(CloudsEngine::inputGain(1.0f) == 2.0f);

  char buf[32];
  info.params[9].format(option(0), buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Granular");
  info.params[9].format(option(1), buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Stretch");
  info.params[9].format(option(2), buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Looping delay");
  info.params[9].format(option(3), buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Spectral");
  info.params[10].format(option(0), buf, sizeof(buf));
  REQUIRE(std::string(buf) == "16-bit stereo");
  info.params[10].format(option(1), buf, sizeof(buf));
  REQUIRE(std::string(buf) == "16-bit mono");
  info.params[10].format(option(2), buf, sizeof(buf));
  REQUIRE(std::string(buf) == "8-bit stereo");
  info.params[10].format(option(3), buf, sizeof(buf));
  REQUIRE(std::string(buf) == "8-bit mono");
  info.params[8].format(0.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Off");
  info.params[8].format(0.5f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "On");
  info.params[3].format(0.75f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "+12.0 st");
  info.params[0].format(0.25f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "25%");
}

TEST_CASE("Granular Mode and Quality are append-only lists in 16 steps") {
  REQUIRE(CloudsEngine::modeIndex(0.5f / 16) == 0);
  REQUIRE(CloudsEngine::modeIndex(1.5f / 16) == 1);
  REQUIRE(CloudsEngine::modeIndex(2.5f / 16) == 2);
  REQUIRE(CloudsEngine::modeIndex(3.5f / 16) == 3);
  REQUIRE(CloudsEngine::modeIndex(15.5f / 16) == 3);  // past the last option: the last
  REQUIRE(CloudsEngine::qualityIndex(0.5f / 16) == 0);
  REQUIRE(CloudsEngine::qualityIndex(1.5f / 16) == 1);
  REQUIRE(CloudsEngine::qualityIndex(2.5f / 16) == 2);
  REQUIRE(CloudsEngine::qualityIndex(3.5f / 16) == 3);
  REQUIRE(CloudsEngine::qualityIndex(1.0f) == 3);
  REQUIRE(CloudsEngine::modeIndex(std::nanf("")) == 0);
}

TEST_CASE("Granular matches bare Clouds at 32 kHz in every mode and quality") {
  const int mode = GENERATE(0, 1, 2, 3);
  const int quality = GENERATE(0, 1, 2, 3);
  INFO("mode " << mode << " quality " << quality);
  ParamValues p = defaults();
  p.p[9] = option(mode);
  p.p[10] = option(quality);
  p.p[11] = 0.25f;  // Spread
  p.p[6] = 0.25f;   // Feedback
  p.p[7] = 0.5f;    // Reverb
  // 3 s of noise and a trigger at 1 s.
  const auto in = noise(32000 * 3);
  const size_t triggerAt = 32000;
  CloudsEngine engine;
  engine.prepare(CloudsEngine::kNativeRate, 256);
  stmlib::Random::Seed(1);
  const Stereo out = run(
      engine, in, 256, [&](size_t) { return p; },
      [&](size_t t) {
        if (t == triggerAt) engine.noteOn(60.0f, 1.0f);
      });
  Reference reference;
  stmlib::Random::Seed(1);
  const Stereo ref = reference.run(in, [&](size_t) { return p; }, [&](size_t t) { return t == triggerAt; });
  REQUIRE(anyNonZero(ref.l, 32000));
  REQUIRE(out.l == ref.l);
  REQUIRE(out.r == ref.r);
}

TEST_CASE("Granular follows moving controls like bare Clouds") {
  // Every continuous control and Freeze move every 0.25 s, notes trigger; Mode and Quality stay.
  auto params = [](size_t t) {
    ParamValues p = test::defaultParams(CloudsEngine::staticInfo());
    const size_t phase = t / 8000;
    for (int k : {0, 1, 2, 3, 4, 5, 6, 7, 11, 12}) p.p[k] = float((phase + size_t(k)) % 5) / 4.0f;
    p.p[8] = phase % 7 == 3 ? 1.0f : 0.0f;
    return p;
  };
  const auto in = noise(32000 * 3, 7);
  auto trigger = [](size_t t) { return t % 4000 == 0 && t > 0; };
  CloudsEngine engine;
  engine.prepare(CloudsEngine::kNativeRate, 256);
  stmlib::Random::Seed(1);
  const Stereo out = run(engine, in, 32, params, [&](size_t t) {
    if (trigger(t)) engine.noteOn(60.0f, 1.0f);
  });
  Reference reference;
  stmlib::Random::Seed(1);
  const Stereo ref = reference.run(in, params, trigger);
  REQUIRE(out.l == ref.l);
  REQUIRE(out.r == ref.r);
}

TEST_CASE("A note-on is one Granular trigger") {
  // Density 50 %: Granular mode makes no grains of its own (granular_processor.cc:97-102), so the
  // wet signal stays silent until a note-on seeds one grain.
  ParamValues p = defaults();
  p.p[0] = 1.0f;  // Blend: wet only
  p.p[4] = 0.5f;  // Density
  const auto in = noise(32000 * 2, 3);
  const size_t noteAt = 32000;
  CloudsEngine engine;
  engine.prepare(CloudsEngine::kNativeRate, 256);
  stmlib::Random::Seed(1);
  const Stereo out = run(
      engine, in, 256, [&](size_t) { return p; },
      [&](size_t t) {
        if (t == noteAt) engine.noteOn(60.0f, 1.0f);
      });
  const double before = maxAbs(out.l, 3200, noteAt);  // after Blend's own first-block glide
  const double after = maxAbs(out.l, noteAt, noteAt + 16000);
  INFO("before " << before << " after " << after);
  REQUIRE(before == 0.0);
  REQUIRE(after > 0.01);
  // One trigger: the grain is delivered once, for one native block; with no further notes, the
  // output falls silent again once the grain ends.
  REQUIRE(maxAbs(out.l, out.l.size() - 3200, out.l.size()) == 0.0);
}

TEST_CASE("Granular In Gain 50 % maps +-1.0 to int16 full scale") {
  // Preflight ruling G9: In Gain 50 % is gain 1, so EG13's +-1.0 reaches Clouds' int16 full scale
  // (as VCV's +-5 V does at gain 1). At Blend 0 the dry path is Clouds' own law: the crossfade's
  // fade_out(0) = 0.7071, then SoftConvert halves and soft-limits: about -9.1 dB.
  REQUIRE(CloudsEngine::inputGain(0.5f) == 1.0f);
  ParamValues p = defaults();
  p.p[0] = 0.0f;  // Blend 0: dry only
  const auto in = sine(32000, 1000.0, 32000.0, 0.5f);
  CloudsEngine engine;
  engine.prepare(CloudsEngine::kNativeRate, 256);
  const Stereo out = run(engine, in, 256, p);
  const double peak = maxAbs(out.l, 3200, out.l.size());
  const double expected = double(stmlib::SoftLimit(0.5f * 0.70710677f * 0.5f));
  INFO("peak " << peak << " expected " << expected << " (" << 20.0 * std::log10(peak / 0.5) << " dB)");
  REQUIRE_THAT(peak, WithinAbs(expected, 1e-4));
}

TEST_CASE("Maximum feedback and reverb stay bounded for 60 s") {
  ParamValues p = defaults();
  p.p[6] = 1.0f;   // Feedback
  p.p[7] = 1.0f;   // Reverb
  p.p[12] = 1.0f;  // In Gain: hot
  const size_t rate = 32000;
  const size_t n = 60 * rate;
  const auto in = noise(n, 5);
  auto params = [&](size_t t) {
    ParamValues q = p;
    q.p[8] = (t / (5 * rate)) % 2 == 1 ? 1.0f : 0.0f;  // Freeze toggles every 5 s
    return q;
  };
  CloudsEngine engine;
  engine.prepare(CloudsEngine::kNativeRate, 256);
  stmlib::Random::Seed(1);
  const Stereo out = run(engine, in, 256, params, [](size_t) {});
  REQUIRE(allFinite(out.l));
  REQUIRE(allFinite(out.r));
  REQUIRE(anyNonZero(out.l, n - rate));
  // Clouds' int16 output can never leave +-1, so "bounded" is checked as no runaway: the last
  // 10 s neither sit at full scale nor grow louder than 10-20 s (the same Freeze phases).
  auto fullScale = [&](const std::vector<float>& x) {
    size_t count = 0;
    for (size_t i = n - 10 * rate; i < n; ++i)
      if (std::fabs(x[i]) >= 0.999f) ++count;
    return double(count) / double(10 * rate);
  };
  const double clippedL = fullScale(out.l), clippedR = fullScale(out.r);
  const double early = 0.5 * (test::rms(out.l, 10 * rate, 20 * rate) + test::rms(out.r, 10 * rate, 20 * rate));
  const double late = 0.5 * (test::rms(out.l, n - 10 * rate, n) + test::rms(out.r, n - 10 * rate, n));
  const double growthDb = 20.0 * std::log10(late / early);
  INFO("full scale L " << clippedL << " R " << clippedR << ", RMS 10-20 s " << early << ", last 10 s " << late
                       << " (" << growthDb << " dB)");
  // Measured (mac, 2026-10-23): no full-scale samples, -0.7 dB.
  REQUIRE(clippedL < 0.01);
  REQUIRE(clippedR < 0.01);
  REQUIRE(growthDb < 3.0);
}

TEST_CASE("Granular Mode and Quality changes fade") {
  // Preflight ruling G10: a change fades out over 10 ms, applies, then fades in over 10 ms. Clouds
  // alone re-inits at once (and goes silent on a change of quality or to or from Spectral).
  ParamValues p = defaults();
  p.p[4] = 0.0f;  // Density: no diffusion in Looping delay and Stretch
  const size_t rate = 32000;
  const auto in = sine(8 * rate, 100.0, double(rate), 0.5f);
  const double inputStep = maxStep(in, 0, in.size());
  // Each second a different Mode and Quality, starting in Looping delay at 16-bit stereo.
  const int modes[8] = {2, 3, 2, 0, 1, 2, 2, 3};
  const int qualities[8] = {0, 0, 2, 2, 1, 3, 0, 1};
  auto params = [&](size_t t) {
    ParamValues q = p;
    q.p[9] = option(modes[t / rate]);
    q.p[10] = option(qualities[t / rate]);
    return q;
  };
  CloudsEngine engine;
  engine.prepare(CloudsEngine::kNativeRate, 256);
  stmlib::Random::Seed(1);
  const Stereo out = run(engine, in, 32, params, [](size_t) {});
  for (size_t s = 1; s < 8; ++s) {
    const size_t at = s * rate;
    INFO("change at " << s << " s");
    // The step size the change itself may cause: the fade's, within 2x the input's own.
    const double step = std::max(maxStep(out.l, at - 32, at + 2 * 640), maxStep(out.r, at - 32, at + 2 * 640));
    INFO("largest step " << step << " input's " << inputStep);
    REQUIRE(step <= 2.0 * inputStep);
    // 10 ms out, then 10 ms in: silent in between, and the sound is back after the fade.
    REQUIRE(maxAbs(out.l, at + 320, at + 352) < 0.02);
    REQUIRE(maxAbs(out.l, at + 640, at + rate) > 0.05);
  }

  // No allocation through changes of Mode and Quality.
  CloudsEngine quiet;
  quiet.prepare(CloudsEngine::kNativeRate, 256);
  std::vector<float> l(256), r(256);
  const float* i2[2] = {in.data(), in.data()};
  float* o2[2] = {l.data(), r.data()};
  quiet.process(i2, o2, params(0), 256);
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int block = 0; block < 2000; ++block) quiet.process(i2, o2, params(size_t(block / 250) * rate), 256);
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}

TEST_CASE("Granular reset after 2 s of audio equals a fresh engine") {
  ParamValues p = defaults();
  p.p[6] = 0.5f;
  p.p[7] = 0.5f;
  const auto in = noise(32000 * 2, 11);
  CloudsEngine used;
  used.prepare(CloudsEngine::kNativeRate, 256);
  ParamValues other = p;
  other.p[9] = option(2);  // played in another Mode and Quality, frozen
  other.p[10] = option(3);
  other.p[8] = 1.0f;
  stmlib::Random::Seed(5);
  run(used, in, 256, p);
  run(used, in, 256, other);
  used.noteOn(60.0f, 1.0f);  // a pending trigger is dropped too
  used.reset();
  CloudsEngine fresh;
  fresh.prepare(CloudsEngine::kNativeRate, 256);
  const auto next = noise(32000, 5);
  stmlib::Random::Seed(1);
  const Stereo a = run(used, next, 256, p);
  stmlib::Random::Seed(1);
  const Stereo b = run(fresh, next, 256, p);
  REQUIRE(anyNonZero(b.l));
  REQUIRE(a.l == b.l);
  REQUIRE(a.r == b.r);
}

TEST_CASE("Granular through the wrapper at 44.1, 48 and 96 kHz with odd blocks") {
  const auto q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  const double rate = GENERATE(44100.0, 48000.0, 96000.0);
  INFO("rate " << rate << " quality " << int(q));
  auto make = [&] {
    auto w = std::make_unique<NativeRateWrapper>(std::make_unique<CloudsEngine>(), CloudsEngine::kNativeRate,
                                                 CloudsEngine::kNativeBlock);
    w->setQuality(q);
    w->setWholeBlocks(true);
    w->prepare(rate, 256);
    return w;
  };
  // Noise for half a second, then grains from the buffer and the reverb's tail: the same output
  // whatever the host slicing. The P's stay put (a P change reaches the engine at the native
  // block its host call lands in, which depends on the slicing).
  const size_t n = size_t(rate * 1.5);
  auto in = noise(n, 9);
  std::fill(in.begin() + long(rate * 0.5), in.end(), 0.0f);
  ParamValues p = defaults();
  p.p[7] = 0.5f;
  auto events = [](size_t) {};
  auto params = [&](size_t) { return p; };
  auto a = make();
  auto b = make();
  auto c = make();
  stmlib::Random::Seed(1);
  const Stereo ra = run(*a, in, 1, params, events);
  stmlib::Random::Seed(1);
  const Stereo rb = run(*b, in, 7, params, events);
  stmlib::Random::Seed(1);
  const Stereo rc = run(*c, in, 129, params, events);
  REQUIRE(allFinite(ra.l));
  REQUIRE(allFinite(ra.r));
  REQUIRE(anyNonZero(ra.l, size_t(rate)));  // grains and reverb play on after the input stops
  REQUIRE(ra.l == rb.l);
  REQUIRE(ra.r == rb.r);
  REQUIRE(ra.l == rc.l);
  REQUIRE(ra.r == rc.r);
  REQUIRE(a->inputUnderruns() == 0);
  REQUIRE(a->inputOverruns() == 0);

  // Blend 0 passes the input dry, so an impulse leaves at the wrapper's reported input-path
  // latency (the engine adds none of its own).
  auto d = make();
  ParamValues dry = defaults();
  dry.p[0] = 0.0f;
  std::vector<float> impulse(size_t(rate * 0.2), 0.0f);
  const size_t at = size_t(rate * 0.05);
  impulse[at] = 1.0f;
  stmlib::Random::Seed(1);
  const Stereo out = run(*d, impulse, 129, dry);
  const int delay = test::peakIndex(out.l) - int(at);
  INFO("delay " << delay << " reported " << d->inputLatencySamples());
  const int tolerance = int(std::ceil(32.0 * rate / 32000.0)) + 2;
  REQUIRE(std::abs(delay - d->inputLatencySamples()) <= tolerance);
}

TEST_CASE("Granular allocates nothing in process, including a Quality change") {
  CloudsEngine engine;
  engine.prepare(CloudsEngine::kNativeRate, 256);
  const auto in = noise(256, 4);
  std::vector<float> l(256), r(256);
  const float* i2[2] = {in.data(), in.data()};
  float* out[2] = {l.data(), r.data()};
  ParamValues p = defaults();
  engine.process(i2, out, p, 256);
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int block = 0; block < 400; ++block) {
      for (int k : {0, 1, 2, 3, 4, 5, 6, 7, 11, 12}) p.p[k] = float((block + k) % 7) / 6.0f;
      p.p[8] = block % 20 < 10 ? 0.0f : 1.0f;
      p.p[9] = option((block / 50) % 4);
      p.p[10] = option((block / 100) % 4);
      if (block % 30 == 0) engine.noteOn(60.0f, 1.0f);
      if (block == 200) engine.reset();
      engine.process(block % 3 == 0 ? nullptr : i2, out, p, 1 + block % 256);
    }
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}
