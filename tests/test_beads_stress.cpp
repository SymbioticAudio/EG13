// Granular Delay stress tests (M5 Task 6). Beads is marked "as is" upstream, so these run the
// engine as the Part FX slot does (inside a NativeRateWrapper at the host rate, whole 16-sample
// blocks, both converter modes) for minutes of hostile input and check what can fail: non-finite
// samples, a stuck-at-full-scale output, runaway or collapsing level between windows, silence
// where sound is due, and sound where silence is due.
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "alloc_counter.h"
#include "engine/beads_engine.h"
#include "engine/native_rate_wrapper.h"
#include "stmlib/utils/random.h"
#include "test_helpers.h"

using namespace eg13;

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 128;

// P indices (0-based) of the Granular Delay P's.
enum : int { DryWet = 0, Time = 1, Size = 2, Pitch = 3, Density = 4, Shape = 5, Feedback = 6, Reverb = 7,
             Freeze = 8, QualityP = 9, SeedP = 10 };

float option(int k) { return (float(k) + 0.5f) / 16.0f; }

// A small deterministic generator (the tests must not touch stmlib::Random's stream).
struct Lcg {
  uint32_t s;
  explicit Lcg(uint32_t seed) : s(seed) {}
  uint32_t next() { s = s * 1664525u + 1013904223u; return s >> 8; }
  float unit() { return float(next()) / float(1 << 24); }  // [0, 1)
  float bipolar() { return unit() - 0.5f; }                // [-0.5, 0.5)
};

std::unique_ptr<NativeRateWrapper> makeSlot(ConverterQuality mode) {
  auto w = std::make_unique<NativeRateWrapper>(std::make_unique<BeadsEngine>(), 0.0, 16);
  w->setQuality(mode);
  w->setWholeBlocks(true);
  w->prepare(kRate, kBlock);
  return w;
}

// Statistics over fixed windows of the output, gathered without allocating while it runs.
struct Windows {
  size_t length;
  std::vector<double> sumSquares;
  std::vector<size_t> rail;  // samples at or beyond 0.999
  size_t nonFinite = 0;
  float peak = 0.0f;
  size_t total = 0;

  Windows(size_t windowLength, size_t windows)
      : length(windowLength), sumSquares(windows, 0.0), rail(windows, 0) {}

  void add(const float* l, const float* r, int n) {
    for (int i = 0; i < n; ++i, ++total) {
      const size_t w = std::min(total / length, sumSquares.size() - 1);
      for (float v : {l[i], r[i]}) {
        if (!std::isfinite(v)) { ++nonFinite; continue; }
        sumSquares[w] += double(v) * double(v);
        peak = std::max(peak, std::fabs(v));
        if (std::fabs(v) >= 0.999f) ++rail[w];
      }
    }
  }
  double rms(size_t w) const { return std::sqrt(sumSquares[w] / (2.0 * double(length))); }
  size_t railTotal() const { size_t t = 0; for (size_t r : rail) t += r; return t; }
  double railFraction() const { return double(railTotal()) / (2.0 * double(total)); }
};

// Feeds `slot` blocks of noise at `level` (0 = silence) and returns nothing; the caller owns the
// loop, this only fills the buffers.
struct Block {
  float in[kBlock], l[kBlock], r[kBlock];
  void fill(Lcg& rng, float level) {
    for (float& v : in) v = 2.0f * level * rng.bipolar();
  }
  void run(NativeRateWrapper& slot, const ParamValues& p, int n = kBlock) {
    const float* i2[2] = {in, in};
    float* o2[2] = {l, r};
    slot.process(i2, o2, p, n);
  }
};

ParamValues defaults() { return test::defaultParams(BeadsEngine::staticInfo()); }

}  // namespace

TEST_CASE("Ten minutes of random automation stay finite and bounded", "[stress]") {
  const auto mode = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  INFO("mode " << int(mode));
  const size_t samples = size_t(kRate * 600.0);
  const size_t window = size_t(kRate * 10.0);
  Windows stats(window, 60);
  auto slot = makeSlot(mode);
  Lcg rng(77);
  Block b;
  ParamValues p = defaults();
  const size_t jump = size_t(kRate * 0.1);
  size_t nextJump = 0;
  float level = 0.5f;
  size_t allocations = 0;
  stmlib::Random::Seed(1);
  {
    test::ScopedAllocationCounter counter;
    for (size_t done = 0; done < samples; done += kBlock) {
      if (done >= nextJump) {  // every 100 ms each P jumps to a seeded random value
        nextJump += jump;
        for (int i = 0; i < 16; ++i) p.p[i] = rng.unit();
        level = rng.unit() < 0.2f ? 0.0f : 0.1f + 0.9f * rng.unit();  // sometimes silent, sometimes hot
        if (rng.unit() < 0.3f) slot->noteOn(36.0f + float(rng.next() % 48), rng.unit());
      }
      b.fill(rng, level);
      b.run(*slot, p);
      stats.add(b.l, b.r, kBlock);
    }
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
  REQUIRE(stats.nonFinite == 0);
  REQUIRE(stats.peak <= 1.0f);
  // Not stuck at full scale, not silent, not running away or dying between windows.
  INFO("rail fraction " << stats.railFraction() << " peak " << stats.peak);
  REQUIRE(stats.railFraction() < 0.02);
  double lo = 1e9, hi = 0.0;
  for (size_t w = 0; w < 60; ++w) {
    lo = std::min(lo, stats.rms(w));
    hi = std::max(hi, stats.rms(w));
  }
  INFO("window rms " << lo << " .. " << hi);
  REQUIRE(lo > 0.01);
  REQUIRE(hi < 0.7);
  REQUIRE(hi < 4.0 * lo);
}

TEST_CASE("Maximum feedback and reverb stay bounded for 60 s", "[stress]") {
  const auto mode = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  const int quality = GENERATE(0, 1, 2, 3);
  INFO("mode " << int(mode) << " quality " << quality);
  Windows stats(size_t(kRate * 10.0), 6);
  auto slot = makeSlot(mode);
  Lcg rng(5);
  Block b;
  ParamValues p = defaults();
  p.p[DryWet] = 1.0f;
  p.p[Feedback] = 1.0f;
  p.p[Reverb] = 1.0f;
  p.p[QualityP] = option(quality);
  stmlib::Random::Seed(1);
  for (size_t done = 0; done < size_t(kRate * 60.0); done += kBlock) {
    b.fill(rng, 0.5f);
    b.run(*slot, p);
    stats.add(b.l, b.r, kBlock);
  }
  REQUIRE(stats.nonFinite == 0);
  REQUIRE(stats.peak <= 1.0f);
  INFO("rail fraction " << stats.railFraction() << " rms first " << stats.rms(0) << " last " << stats.rms(5));
  REQUIRE(stats.railFraction() < 0.02);
  REQUIRE(stats.rms(1) > 0.01);  // sound, not silence
  // No runaway or collapse: the level of the last window stays near the second one's.
  REQUIRE(stats.rms(5) < 2.0 * stats.rms(1));
  REQUIRE(stats.rms(5) > 0.5 * stats.rms(1));
}

TEST_CASE("Freeze for five minutes at every Time and Size extreme", "[stress]") {
  const auto mode = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  const float time = GENERATE(0.0f, 1.0f);
  const float size = GENERATE(0.0f, 1.0f);  // Size 1 is delay mode
  INFO("mode " << int(mode) << " time " << time << " size " << size);
  Windows stats(size_t(kRate * 60.0), 5);
  auto slot = makeSlot(mode);
  Lcg rng(21);
  Block b;
  ParamValues p = defaults();
  p.p[DryWet] = 1.0f;
  p.p[Time] = time;
  p.p[Size] = size;
  p.p[Density] = 0.75f;
  p.p[Feedback] = 0.5f;
  p.p[Reverb] = 0.5f;
  stmlib::Random::Seed(1);
  const size_t samples = size_t(kRate * 300.0);
  for (size_t done = 0; done < samples; done += kBlock) {
    p.p[Freeze] = done >= size_t(kRate * 30.0) ? 1.0f : 0.0f;  // thirty seconds of recording (delay mode at Time 1 reaches back seconds), then frozen
    b.fill(rng, 0.5f);  // the input keeps playing into the frozen buffer
    b.run(*slot, p);
    stats.add(b.l, b.r, kBlock);
  }
  REQUIRE(stats.nonFinite == 0);
  REQUIRE(stats.peak <= 1.0f);
  INFO("rail fraction " << stats.railFraction() << " rms " << stats.rms(0) << " " << stats.rms(2) << " " << stats.rms(4));
  REQUIRE(stats.railFraction() < 0.02);
  for (size_t w = 1; w < 5; ++w) REQUIRE(stats.rms(w) > 0.01);  // the frozen buffer keeps sounding
  REQUIRE(stats.rms(4) < 2.0 * stats.rms(1));
  REQUIRE(stats.rms(4) > 0.5 * stats.rms(1));
}

TEST_CASE("Delay-mode transitions while Size crosses 0.98-0.99 repeatedly", "[stress]") {
  // processor.cc:110-117 switches to delay mode at Size >= 0.98 (and back below 0.99 with
  // hysteresis); sweep through that band at several rates, with and without Freeze.
  const auto mode = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  const bool freeze = GENERATE(false, true);
  INFO("mode " << int(mode) << " freeze " << freeze);
  Windows stats(size_t(kRate * 10.0), 6);
  auto slot = makeSlot(mode);
  Lcg rng(31);
  Block b;
  ParamValues p = defaults();
  p.p[DryWet] = 1.0f;
  p.p[Density] = 0.75f;
  p.p[Feedback] = 0.5f;
  p.p[Reverb] = 0.3f;
  stmlib::Random::Seed(1);
  const size_t samples = size_t(kRate * 60.0);
  size_t crossings = 0;
  bool inside = false;
  for (size_t done = 0; done < samples; done += kBlock) {
    const double t = double(done) / kRate;
    const double period = 0.05 + 0.5 * std::fmod(t / 10.0, 1.0);  // 50 ms .. 550 ms sweeps
    const double tri = std::fabs(2.0 * std::fmod(t / period, 1.0) - 1.0);
    p.p[Size] = float(0.97 + 0.025 * tri);                          // 0.970 .. 0.995
    const bool nowInside = p.p[Size] >= 0.985f;
    if (nowInside != inside) ++crossings;
    inside = nowInside;
    p.p[Time] = float(0.2 + 0.6 * std::fabs(std::sin(t * 0.37)));
    p.p[Freeze] = (freeze && std::fmod(t, 7.0) > 3.5) ? 1.0f : 0.0f;
    b.fill(rng, 0.5f);
    b.run(*slot, p);
    stats.add(b.l, b.r, kBlock);
  }
  REQUIRE(crossings > 200);  // the sweep really crosses the band over and over
  REQUIRE(stats.nonFinite == 0);
  REQUIRE(stats.peak <= 1.0f);
  INFO("rail fraction " << stats.railFraction());
  REQUIRE(stats.railFraction() < 0.02);
  double lo = 1e9, hi = 0.0;
  for (size_t w = 0; w < 6; ++w) {
    lo = std::min(lo, stats.rms(w));
    hi = std::max(hi, stats.rms(w));
  }
  INFO("window rms " << lo << " .. " << hi);
  REQUIRE(lo > 0.01);
  REQUIRE(hi < 2.0 * lo);
}

TEST_CASE("Silence in, silence out after the tail", "[stress]") {
  const auto mode = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  const int quality = GENERATE(0, 1, 2, 3);
  INFO("mode " << int(mode) << " quality " << quality);
  auto slot = makeSlot(mode);
  Lcg rng(13);
  Block b;
  ParamValues p = defaults();
  p.p[DryWet] = 1.0f;
  p.p[Feedback] = 0.0f;
  p.p[Freeze] = 0.0f;
  p.p[Reverb] = 0.5f;
  p.p[QualityP] = option(quality);
  stmlib::Random::Seed(1);
  float heard = 0.0f;
  for (size_t done = 0; done < size_t(kRate * 12.0); done += kBlock) {  // 12 s of noise in: Time 0.5 reaches back seconds
    b.fill(rng, 0.5f);
    b.run(*slot, p);
    for (float v : b.l) heard = std::max(heard, std::fabs(v));
  }
  REQUIRE(heard > 0.05f);  // there was sound to fall silent from
  float tail = 0.0f;
  double sum = 0.0;
  size_t count = 0;
  const size_t silent = size_t(kRate * 30.0);
  for (size_t done = 0; done < silent; done += kBlock) {
    std::fill(b.in, b.in + kBlock, 0.0f);
    b.run(*slot, p);
    if (done >= silent - size_t(kRate * 5.0)) {  // the last five seconds
      for (int i = 0; i < kBlock; ++i) {
        tail = std::max({tail, std::fabs(b.l[i]), std::fabs(b.r[i])});
        sum += double(b.l[i]) + double(b.r[i]);
        ++count;
      }
    }
  }
  INFO("tail peak " << tail << " mean " << sum / double(count));
  REQUIRE(std::isfinite(tail));
  REQUIRE(tail < 1e-4f);
}
