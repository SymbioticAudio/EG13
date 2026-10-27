#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

#include "stmlib/utils/random.h"
#include "alloc_counter.h"
#include "engine/engine_registry.h"
#include "engine/warps_engine.h"
#include "util/dc_blocker.h"
#include "test_engines.h"
#include "test_helpers.h"
#include "voice/processor_factory.h"
#include "voice/processor_slot.h"

using namespace eg13;

namespace {

constexpr double kRate = 48000.0;

struct Stereo {
  std::vector<float> l, r;
};

// `side`, when given, is every side signal (Source AUX, Input and Other part) at once.
Stereo run(ProcessorSlot& slot, const std::vector<float>& l, const std::vector<float>& r, const ParamValues& p,
           const float* side = nullptr) {
  Stereo s{std::vector<float>(l.size()), std::vector<float>(l.size())};
  for (size_t done = 0; done < l.size(); done += 16) {
    const int n = int(std::min<size_t>(16, l.size() - done));
    const float* in[2] = {l.data() + done, r.data() + done};
    float* out[2] = {s.l.data() + done, s.r.data() + done};
    const float* at = side ? side + done : nullptr;
    const SideSignals sides{at, at, at};
    slot.process(in, &sides, out, p, n);
  }
  return s;
}

void append(Stereo& to, const Stereo& more) {
  to.l.insert(to.l.end(), more.l.begin(), more.l.end());
  to.r.insert(to.r.end(), more.r.begin(), more.r.end());
}

std::vector<float> sine(size_t n, double hz, float amp = 0.5f, double rate = kRate) {
  std::vector<float> x(n);
  for (size_t i = 0; i < n; ++i) x[i] = amp * float(std::sin(6.283185307179586 * hz * double(i) / rate));
  return x;
}

ParamValues gainParams(float gain) {  // GainEngine: out = in * 4 * P1
  ParamValues p;
  p.p[0] = gain / 4.0f;
  return p;
}

std::vector<SourceSpec> twoGains() {
  std::vector<SourceSpec> specs;
  specs.push_back({std::make_unique<test::GainEngine>(), 0.0, 16});
  specs.push_back({std::make_unique<test::GainEngine>(), 0.0, 16});
  return specs;
}

// Three gains, padded by a fourth engine with 60 samples of latency: a switch holds for 60
// samples, then fades over 480 (10 ms at 48 kHz).
std::vector<SourceSpec> threeGainsWithHold() {
  std::vector<SourceSpec> specs = twoGains();
  specs.push_back({std::make_unique<test::GainEngine>(), 0.0, 16});
  specs.push_back({std::make_unique<test::FixedLatencyEngine>(), 0.0, 16});
  return specs;
}

float maxStep(const std::vector<float>& x) {
  float step = 0.0f;
  for (size_t i = 1; i < x.size(); ++i) step = std::max(step, std::fabs(x[i] - x[i - 1]));
  return step;
}

}  // namespace

TEST_CASE("The Filter slot has no latency at common host rates") {
  const auto q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  for (double rate : {44100.0, 48000.0, 96000.0}) {
    ProcessorSlot filters(makeAllFilters());
    filters.prepare(rate, 16, q);
    REQUIRE(filters.latencySamples() == 0);
  }
}

TEST_CASE("Every Shaper engine is padded to one slot latency") {
  const auto q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  for (double rate : {44100.0, 48000.0, 96000.0}) {
    ProcessorSlot shapers(makeAllShapers());
    shapers.prepare(rate, 16, q);
    const int latency = shapers.latencySamples();
    INFO("Shaper slot latency at " << rate << " Hz: " << latency << " samples");
    REQUIRE(latency > 0);
    std::vector<float> impulse(4800, 0.0f);
    impulse[1000] = 0.5f;
    ParamValues off;
    REQUIRE(test::peakIndex(run(shapers, impulse, impulse, off).l) == 1000 + latency);  // Off: exactly

    shapers.setEngine(int(ShaperEngine::Eq));
    shapers.reset();
    ParamValues flat = test::defaultParams(shaperEngineInfo(int(ShaperEngine::Eq)));
    REQUIRE(std::abs(test::peakIndex(run(shapers, impulse, impulse, flat).l) - (1000 + latency)) <= 8);

    shapers.setEngine(int(ShaperEngine::RingMod));
    shapers.reset();
    ParamValues carrierOnly = test::defaultParams(shaperEngineInfo(int(ShaperEngine::RingMod)));
    carrierOnly.p[0] = 0.0f;  // crossfade ...
    carrierOnly.p[1] = 0.0f;  // ... all the way to the carrier
    carrierOnly.p[2] = 0.5f;
    carrierOnly.p[4] = 1.5f / 8.0f;  // Carrier: Voice
    carrierOnly.p[5] = 2.5f / 8.0f;  // Modulator: Input (absent: silence)
    const int ringMod = test::peakIndex(run(shapers, impulse, impulse, carrierOnly).l);
    INFO("Ring Mod impulse at " << ringMod - 1000 << " samples");
    REQUIRE(std::abs(ringMod - (1000 + latency)) <= 16);
  }
}

TEST_CASE("Switching engines while sounding crossfades without a step") {
  ProcessorSlot slot(twoGains());
  slot.prepare(kRate, 16);
  const std::vector<float> dc(4800, 0.5f);
  ParamValues before;
  before.p[0] = 0.25f;  // gain 1
  run(slot, dc, dc, before);
  slot.setEngine(1);
  ParamValues after;
  after.p[0] = 0.75f;  // gain 3, for the new engine; the old one keeps 0.25
  const Stereo out = run(slot, dc, dc, after);
  REQUIRE(std::fabs(out.l[0] - 0.5f) < 0.01f);  // starts where it was
  for (size_t i = 1; i < out.l.size(); ++i) {
    REQUIRE(out.l[i] >= out.l[i - 1]);
    REQUIRE(out.l[i] - out.l[i - 1] <= 1.0f / 480.0f * 1.01f + 1e-6f);
  }
  REQUIRE(out.l.back() == 1.5f);
  REQUIRE_FALSE(slot.fading());
}

TEST_CASE("Switching again during a fade waits for the fade to end, without a step") {
  // Each request is made after `delay` samples of the first switch: inside the 60-sample hold,
  // and halfway through the fade.
  for (size_t delay : {size_t(30), size_t(60 + 240)}) {
    INFO("second request " << delay << " samples after the first");
    ProcessorSlot slot(threeGainsWithHold());
    slot.prepare(kRate, 16);
    REQUIRE(slot.latencySamples() == test::FixedLatencyEngine::kDelay);
    const std::vector<float> dc(4800, 0.5f);
    run(slot, dc, dc, gainParams(1.0f));  // engine 0 at 0.5
    slot.setEngine(1);
    Stereo out = run(slot, std::vector<float>(delay, 0.5f), std::vector<float>(delay, 0.5f), gainParams(3.0f));
    REQUIRE(slot.fading());
    // The host now shows engine 2's values; engine 1 keeps its own (gain 3) until it is fully in.
    slot.setEngine(2);
    REQUIRE(slot.engine() == 1);
    REQUIRE(slot.pendingEngine() == 2);
    append(out, run(slot, dc, dc, gainParams(2.0f)));
    float largest = 0.0f;
    for (size_t i = 1; i < out.l.size(); ++i) {
      INFO("sample " << i);
      REQUIRE(std::fabs(out.l[i] - out.l[i - 1]) <= 1.0f / 480.0f * 1.01f + 1e-6f);
      largest = std::max(largest, out.l[i]);
    }
    REQUIRE(largest == 1.5f);         // engine 1 faded fully in, with its own setting ...
    REQUIRE(out.l.back() == 1.0f);    // ... then engine 2 took over
    REQUIRE(slot.engine() == 2);
    REQUIRE(slot.pendingEngine() == -1);
    REQUIRE_FALSE(slot.fading());
  }
}

TEST_CASE("Asking for the fading-in engine again cancels a waiting request") {
  ProcessorSlot slot(threeGainsWithHold());
  slot.prepare(kRate, 16);
  const std::vector<float> dc(4800, 0.5f);
  run(slot, dc, dc, gainParams(1.0f));
  slot.setEngine(1);
  run(slot, std::vector<float>(160, 0.5f), std::vector<float>(160, 0.5f), gainParams(3.0f));
  slot.setEngine(2);
  slot.setEngine(1);
  REQUIRE(slot.pendingEngine() == -1);
  const Stereo out = run(slot, dc, dc, gainParams(3.0f));
  REQUIRE(out.l.back() == 1.5f);
  REQUIRE(slot.engine() == 1);
  REQUIRE_FALSE(slot.fading());
}

TEST_CASE("restart() and reset() go straight to a waiting request") {
  for (bool full : {false, true}) {
    INFO((full ? "reset()" : "restart()"));
    ProcessorSlot slot(threeGainsWithHold());
    slot.prepare(kRate, 16);
    const std::vector<float> dc(4800, 0.5f);
    run(slot, dc, dc, gainParams(1.0f));
    slot.setEngine(1);
    slot.setEngine(2);
    REQUIRE(slot.pendingEngine() == 2);
    if (full) {
      slot.reset();
    } else {
      slot.restart();
    }
    REQUIRE(slot.engine() == 2);
    REQUIRE(slot.pendingEngine() == -1);
    REQUIRE_FALSE(slot.fading());
    // Engine 2 alone at full weight: silent for its (fresh) 60-sample delay, then gain 2.
    const Stereo out = run(slot, dc, dc, gainParams(2.0f));
    for (size_t i = 0; i < 60; ++i) REQUIRE(out.l[i] == 0.0f);
    for (size_t i = 60; i < out.l.size(); ++i) REQUIRE(out.l[i] == 1.0f);
  }
}

TEST_CASE("A switch right after reset() or restart() is immediate: no fade from stale settings") {
  // Found in review: reset() left the outgoing engine with all-zero parameters, which for Ring
  // Mod is a full-level internal oscillator (a 1.12 peak for 16 ms from silent input).
  for (bool full : {false, true}) {
    INFO((full ? "reset()" : "restart()"));
    ProcessorSlot shapers(makeAllShapers());
    shapers.prepare(kRate, 16);
    shapers.setEngine(int(ShaperEngine::RingMod));
    const std::vector<float> silence(4800, 0.0f);
    run(shapers, silence, silence, test::defaultParams(shaperEngineInfo(int(ShaperEngine::RingMod))));
    if (full) {
      shapers.reset();
    } else {
      shapers.restart();
    }
    shapers.setEngine(int(ShaperEngine::Eq));
    REQUIRE(shapers.engine() == int(ShaperEngine::Eq));
    REQUIRE_FALSE(shapers.fading());
    const Stereo out = run(shapers, silence, silence, test::defaultParams(shaperEngineInfo(int(ShaperEngine::Eq))));
    float peak = 0.0f;
    for (float s : out.l) peak = std::max(peak, std::fabs(s));
    REQUIRE(peak == 0.0f);  // EQ of silence
  }
  // restart() with a request waiting: the waiting engine starts, and a switch before the next
  // block is immediate too (it would otherwise fade out with another engine's settings).
  ProcessorSlot shapers(makeAllShapers());
  shapers.prepare(kRate, 16);
  const std::vector<float> silence(4800, 0.0f);
  run(shapers, silence, silence, ParamValues{});
  shapers.setEngine(int(ShaperEngine::Eq));
  shapers.setEngine(int(ShaperEngine::RingMod));
  shapers.restart();
  REQUIRE(shapers.engine() == int(ShaperEngine::RingMod));
  shapers.setEngine(int(ShaperEngine::Off));
  REQUIRE_FALSE(shapers.fading());
  const Stereo out = run(shapers, silence, silence, ParamValues{});
  for (float s : out.l) REQUIRE(s == 0.0f);
}

TEST_CASE("A Shaper switch holds the old engine for the slot latency, so the level does not dip") {
  ProcessorSlot shapers(makeAllShapers());
  shapers.prepare(kRate, 16);
  const auto x = sine(9600, 1000.0, 0.25f);
  ParamValues off;
  run(shapers, x, x, off);
  shapers.setEngine(int(ShaperEngine::Eq));
  const Stereo out = run(shapers, x, x, test::defaultParams(shaperEngineInfo(int(ShaperEngine::Eq))));
  const double reference = test::rms(x, 0, 4800);
  for (size_t w = 0; w + 48 <= 2400; w += 48) {  // one 1 kHz period per window
    const double db = 20.0 * std::log10(test::rms(out.l, w, w + 48) / reference);
    INFO("window at " << w << ": " << db << " dB");
    REQUIRE(std::fabs(db) < 0.5);
  }
}

TEST_CASE("The outgoing Ring Mod keeps its own side inputs while it fades out") {
  // Ring Mod with Modulator = Input: the internal oscillator times the Input. Switching to EQ,
  // which reads no side input, must not cut the Ring Mod's Input before it has faded out.
  ProcessorSlot shapers(makeAllShapers());
  shapers.prepare(kRate, 16);
  const int latency = shapers.latencySamples();
  const auto voice = sine(9600, 220.0, 0.25f);
  const auto input = sine(9600, 1000.0, 0.25f);
  ParamValues ringMod = test::defaultParams(shaperEngineInfo(int(ShaperEngine::RingMod)));
  ringMod.p[5] = WarpsEngine::optionValue(2);  // Modulator: Input
  shapers.setEngine(int(ShaperEngine::RingMod));
  shapers.reset();
  const Stereo before = run(shapers, voice, voice, ringMod, input.data());
  shapers.setEngine(int(ShaperEngine::Eq));
  const Stereo out = run(shapers, voice, voice, test::defaultParams(shaperEngineInfo(int(ShaperEngine::Eq))),
                         input.data());
  // Windows of 192 samples (4 ms) span a period of the oscillator (C4, 3.8 ms).
  constexpr size_t kWindow = 192;
  const double ringLevel = test::rms(before.l, 4800, 9600);
  const double eqLevel = test::rms(out.l, 4800, 9600);
  INFO("Ring Mod " << 20.0 * std::log10(ringLevel) << " dBFS, EQ " << 20.0 * std::log10(eqLevel) << " dBFS");
  REQUIRE(ringLevel > 0.02);
  const double floor = std::min(ringLevel, eqLevel);
  for (size_t w = 0; w + kWindow <= size_t(latency) + 480 + 2 * kWindow; w += kWindow / 4) {
    const double db = 20.0 * std::log10(test::rms(out.l, w, w + kWindow) / floor);
    INFO("window at " << w << " (hold ends at " << latency << "): " << db << " dB");
    REQUIRE(db > -3.0);  // a linear crossfade of unrelated signals dips 3 dB at most
  }
}

TEST_CASE("The slot tells its engine when the input is stereo, with a hold") {
  auto probe = std::make_unique<test::StereoProbeEngine>();
  test::StereoProbeEngine* raw = probe.get();
  std::vector<SourceSpec> specs;
  specs.push_back({std::move(probe), 0.0, 16});
  ProcessorSlot slot(std::move(specs));
  slot.prepare(kRate, 16);
  const auto l = sine(480, 220.0);
  const auto r = sine(480, 330.0);
  run(slot, l, l, ParamValues{});
  REQUIRE_FALSE(raw->stereo());
  run(slot, l, r, ParamValues{});
  REQUIRE(raw->stereo());
  run(slot, l, l, ParamValues{});  // 10 ms of mono: still stereo (50 ms hold)
  REQUIRE(raw->stereo());
  const std::vector<float> longer = sine(2400, 220.0);
  run(slot, longer, longer, ParamValues{});
  REQUIRE_FALSE(raw->stereo());
}

namespace {

// L is a sine; R equals L, then drifts away smoothly, then returns to L exactly.
Stereo stereoAndBack(ProcessorSlot& slot, const ParamValues& p) {
  const size_t n = 24000;
  const auto l = sine(n, 220.0);
  std::vector<float> r(l);
  for (size_t i = 4800; i < 14400; ++i) {
    const double t = double(i - 4800) / 9600.0;
    r[i] = l[i] * float(1.0 + 0.5 * std::sin(3.14159265358979 * t));
  }
  return run(slot, l, r, p);
}

}  // namespace

TEST_CASE("A real filter goes stereo and back without a jump") {
  for (int engine : {int(FilterEngine::Analog), int(FilterEngine::Classic), int(FilterEngine::StateVariable)}) {
    ProcessorSlot filters(makeAllFilters());
    filters.prepare(kRate, 16);
    filters.setEngine(engine);
    filters.reset();
    ParamValues p = test::defaultParams(filterEngineInfo(engine));
    p.p[0] = 0.6f;
    p.p[1] = 0.5f;
    const Stereo out = stereoAndBack(filters, p);
    INFO("filter " << engine);
    REQUIRE(maxStep(out.r) < 0.08f);  // a cut between instances would jump by up to the signal's size
    REQUIRE(test::rms(out.r, 8000, 12000) > 1.1 * test::rms(out.l, 8000, 12000));  // stereo got through
    REQUIRE_FALSE(filters.stereo());  // back to mono after the hold
  }
}

TEST_CASE("A real shaper goes stereo and back without a jump") {
  for (int engine : {int(ShaperEngine::Eq), int(ShaperEngine::RingMod)}) {
    ProcessorSlot shapers(makeAllShapers());
    shapers.prepare(kRate, 16);
    shapers.setEngine(engine);
    shapers.reset();
    ParamValues p = test::defaultParams(shaperEngineInfo(engine));
    if (engine == int(ShaperEngine::RingMod)) {
      // Ring Mod's right instance cannot copy the left one: it starts fresh and fades in. Set it
      // to pass its carrier, the voice, so left and right are comparable.
      p.p[0] = 0.0f;
      p.p[1] = 0.0f;
      p.p[4] = WarpsEngine::optionValue(1);  // Carrier: Voice
    }
    const Stereo out = stereoAndBack(shapers, p);
    INFO("shaper " << engine);
    REQUIRE(maxStep(out.r) < 0.08f);
    REQUIRE(test::rms(out.r, 8000, 12000) > 1.1 * test::rms(out.l, 8000, 12000));
    REQUIRE_FALSE(shapers.stereo());
  }
}

TEST_CASE("Slot processing, engine switches and stereo changes do not allocate") {
  ProcessorSlot shapers(makeAllShapers());
  ProcessorSlot filters(makeAllFilters());
  shapers.prepare(kRate, 16);
  filters.prepare(kRate, 16);
  std::vector<float> l(16, 0.2f), r(16, -0.2f), side(16, 0.1f), ol(16), orr(16);
  const float* in[2] = {l.data(), r.data()};
  float* out[2] = {ol.data(), orr.data()};
  ParamValues p;
  p.p[0] = 0.5f;
  p.p[4] = WarpsEngine::optionValue(3);  // Ring Mod: Carrier = Input, Modulator = Input
  p.p[5] = WarpsEngine::optionValue(2);
  const SideSignals sides{side.data(), side.data(), side.data()};
  shapers.process(in, &sides, out, p, 16);
  filters.process(in, nullptr, out, p, 16);
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int block = 0; block < 600; ++block) {
      // Every 60 blocks a switch, and 7 blocks later (inside its fade) a second request that waits:
      // block / 7 cycles the engine index, so the pairs ask for different engines in turn.
      if (block % 60 == 0 || block % 60 == 7) {
        shapers.setEngine((block / 7) % kNumShaperEngines);
        filters.setEngine((block / 7) % kNumFilterEngines);
      }
      in[1] = block % 200 < 100 ? r.data() : l.data();
      shapers.process(in, &sides, out, p, 16);
      filters.process(in, nullptr, out, p, 16);
      if (block == 300) {
        shapers.restart();
        filters.restart();
      }
    }
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}

// Voice sends Source AUX to the Shaper without delaying it by the Filter latency, which holds
// only while every Filter has none (voice.cpp asserts it, but Release builds drop the assert).
TEST_CASE("Every Filter type has zero latency at every host rate") {
  for (double rate : {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0}) {
    for (int e = 0; e < kNumFilterEngines; ++e) {
      INFO("rate " << rate << " filter " << filterEngineInfo(e).displayName);
      SourceSpec spec = makeFilter(e);
      NativeRateWrapper wrapper(std::move(spec.engine), spec.nativeRate, spec.nativeBlock);
      wrapper.prepare(rate, 16);
      REQUIRE(wrapper.latencySamples() == 0);
      REQUIRE(wrapper.inputPathLatencySamples() == 0);
    }
    ProcessorSlot slot(makeAllFilters());
    slot.prepare(rate, 16);
    REQUIRE(slot.latencySamples() == 0);
  }
}

TEST_CASE("A Part FX ProcessorSlot pads every engine to the slot latency") {
  const auto q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  for (double rate : {44100.0, 48000.0, 96000.0}) {
    // Each engine alone, in whole-block mode as the real slot runs it.
    int slowest = -1;
    int maxLatency = 0;
    for (int e = 0; e < kNumPartFxEngines; ++e) {
      SourceSpec spec = makePartFx(e);
      NativeRateWrapper w(std::move(spec.engine), spec.nativeRate, spec.nativeBlock);
      w.setWholeBlocks(spec.wholeBlocks);
      w.setQuality(q);
      w.prepare(rate, 16);
      const int own = w.inputPathLatencySamples();
      WARN("latency " << (q == ConverterQuality::Live ? "Live" : "Studio") << " " << rate << " Hz "
                      << partFxEngineInfo(e).displayName << ": " << own << " samples, "
                      << 1000.0 * own / rate << " ms");
      if (own > maxLatency) {
        maxLatency = own;
        slowest = e;
      }
    }
    ProcessorSlot slot(makeAllPartFx());
    slot.prepare(rate, 16, q);
    const int latency = slot.latencySamples();
    WARN("padded slot latency " << (q == ConverterQuality::Live ? "Live" : "Studio") << " " << rate << " Hz: "
                                << latency << " samples, " << 1000.0 * latency / rate << " ms; set by "
                                << partFxEngineInfo(slowest).displayName);
    REQUIRE(latency == maxLatency);
    REQUIRE(latency > 0);
    // Every engine, through the real slot, lands an impulse at exactly the slot latency. The
    // engines run dry (P1 = Blend / Dry-Wet / Mix / Amount at 0, the other P's at their defaults),
    // so the impulse passes through the engine's own converters and whole-block FIFO unchanged
    // and gives a clean onset. Off has no P's.
    std::vector<float> impulse(9600, 0.0f);
    impulse[1000] = 0.5f;
    for (int e = 0; e < kNumPartFxEngines; ++e) {
      ParamValues dry = test::defaultParams(partFxEngineInfo(e));
      dry.p[0] = 0.0f;
      slot.setEngine(e);
      slot.reset();
      INFO(partFxEngineInfo(e).displayName << " at " << rate << " Hz, slot latency " << latency);
      REQUIRE(test::peakIndex(run(slot, impulse, impulse, dry).l) == 1000 + latency);
    }
  }
}

TEST_CASE("A Part FX ProcessorSlot runs a host-rate-bypassed engine in whole native blocks") {
  // Every Part FX engine at 48 kHz, where the Resonator and Granular Delay are bypassed by their
  // wrappers; only the slot's whole-block setting (SourceSpec::wholeBlocks, ruling G8) makes the
  // output independent of the host slicing. Each run draws from the same random sequence.
  const auto q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  const int engine = GENERATE(int(PartFxEngine::Granular), int(PartFxEngine::GranularDelay),
                              int(PartFxEngine::Resonator), int(PartFxEngine::Reverb));
  INFO("engine " << engine);
  std::vector<float> noise(4800);
  unsigned state = 12345u;
  for (auto& x : noise) {
    state = state * 1664525u + 1013904223u;
    x = (float(state >> 8) / float(1u << 24) - 0.5f) * 0.5f;
  }
  auto render = [&](size_t chunk) {
    stmlib::Random::Seed(1);
    ProcessorSlot slot(makeAllPartFx());
    slot.prepare(48000.0, 16, q);
    slot.setEngine(engine);
    slot.reset();
    ParamValues p = test::defaultParams(partFxEngineInfo(engine));
    p.p[0] = engine == int(PartFxEngine::Resonator) ? 1.0f : 0.7f;  // mostly wet
    p.p[4] = 0.8f;                                                   // Granular Density: grains at random
    std::vector<float> out(noise.size());
    for (size_t done = 0; done < noise.size(); done += chunk) {
      const int n = int(std::min(chunk, noise.size() - done));
      const float* in[2] = {noise.data() + done, noise.data() + done};
      float l[16], r[16];
      float* o[2] = {l, r};
      const SideSignals sides{};
      slot.process(in, &sides, o, p, n);
      std::copy(l, l + n, out.begin() + long(done));
    }
    return out;
  };
  const auto whole = render(16);
  REQUIRE(test::rms(whole, 1200, whole.size()) > 0.001);  // it sounds
  REQUIRE(render(7) == whole);
  REQUIRE(render(1) == whole);
}

namespace {

// Every Shaper engine without the slot's DC filter: the engines as they were before it (task 8g).
std::vector<SourceSpec> shapersWithoutDcFilter() {
  std::vector<SourceSpec> all = makeAllShapers();
  for (SourceSpec& s : all) s.dcBlock = false;
  return all;
}

double meanOf(const std::vector<float>& x, size_t from, size_t to) {
  double sum = 0.0;
  for (size_t i = from; i < to; ++i) sum += double(x[i]);
  return sum / double(to - from);
}

// The RMS ratio in dB of `out` to `in` over [from, to).
double gainDb(const std::vector<float>& out, const std::vector<float>& in, size_t from, size_t to) {
  return 20.0 * std::log10(test::rms(out, from, to) / test::rms(in, from, to));
}

}  // namespace

TEST_CASE("Every Shaper and Part FX engine but Off asks for the DC filter") {
  const std::vector<SourceSpec> all = makeAllShapers();
  for (int e = 0; e < kNumShaperEngines; ++e) {
    INFO(shaperEngineInfo(e).displayName);
    REQUIRE(all[size_t(e)].dcBlock == (e != int(ShaperEngine::Off)));
  }
  for (const SourceSpec& s : makeAllFilters()) REQUIRE_FALSE(s.dcBlock);
  const std::vector<SourceSpec> fx = makeAllPartFx();
  for (int e = 0; e < kNumPartFxEngines; ++e) {
    INFO(partFxEngineInfo(e).displayName);
    REQUIRE(fx[size_t(e)].dcBlock == (e != int(PartFxEngine::Off)));
  }
}

TEST_CASE("The Shaper's DC filter removes the Ring Mod's DC") {
  // Fold with the internal oscillator as carrier puts out a large DC on a sine (task 8b2's
  // first Pressed Velvet draft: up to -13 dBFS). The slot's DC filter takes it below -60 dBFS.
  const auto x = sine(2 * size_t(kRate), 110.0, 0.5f);
  ParamValues fold = test::defaultParams(shaperEngineInfo(int(ShaperEngine::RingMod)));
  fold.p[0] = 1.0f / 8.0f;  // Algorithm: Fold
  fold.p[1] = 0.9f;         // Timbre
  fold.p[2] = 0.8f;         // Carrier Level
  fold.p[3] = 0.8f;         // Mod Level
  auto meanDb = [&](std::vector<SourceSpec> engines) {
    ProcessorSlot slot(std::move(engines));
    slot.prepare(kRate, 16);
    slot.setEngine(int(ShaperEngine::RingMod));
    slot.reset();
    slot.noteOn(45.0f, 1.0f);
    const Stereo out = run(slot, x, x, fold);
    const double mean = std::max(std::fabs(meanOf(out.l, size_t(kRate), out.l.size())),
                                 std::fabs(meanOf(out.r, size_t(kRate), out.r.size())));
    return 20.0 * std::log10(mean + 1e-30);
  };
  const double before = meanDb(shapersWithoutDcFilter());
  const double after = meanDb(makeAllShapers());
  INFO("Fold DC over the second second: " << before << " dBFS without the filter, " << after << " dBFS with it");
  REQUIRE(before > -40.0);  // the setting does make DC
  REQUIRE(after < -60.0);
}

TEST_CASE("A sine through Shaper Off is bit-identical with the DC filter in the slot") {
  const auto x = sine(9600, 440.0, 0.5f);
  ParamValues off;
  ProcessorSlot with(makeAllShapers());
  ProcessorSlot without(shapersWithoutDcFilter());
  with.prepare(kRate, 16);
  without.prepare(kRate, 16);
  const Stereo a = run(with, x, x, off);
  const Stereo b = run(without, x, x, off);
  const int latency = with.latencySamples();
  for (size_t i = 0; i < x.size(); ++i) {
    REQUIRE(a.l[i] == b.l[i]);
    REQUIRE(a.r[i] == b.r[i]);
    REQUIRE(a.l[i] == (i >= size_t(latency) ? x[i - size_t(latency)] : 0.0f));
  }
}

TEST_CASE("The DC filter leaves the EQ within 0.01 dB above 40 Hz") {
  // A coloured EQ (low and high shelves and both mids moved), at every host rate.
  ParamValues eq = test::defaultParams(shaperEngineInfo(int(ShaperEngine::Eq)));
  eq.p[1] = 0.8f;  // High Gain
  eq.p[3] = 0.3f;  // Mid 1 Gain
  eq.p[6] = 0.7f;  // Mid 2 Gain
  eq.p[8] = 0.2f;  // Low Freq
  eq.p[9] = 0.85f;  // Low Gain
  for (const double rate : {44100.0, 48000.0, 96000.0, 192000.0}) {
    for (const double hz : {40.0, 60.0, 100.0, 1000.0, 10000.0}) {
      const size_t n = size_t(rate);
      const auto x = sine(n, hz, 0.25f, rate);
      auto level = [&](std::vector<SourceSpec> engines) {
        ProcessorSlot slot(std::move(engines));
        slot.prepare(rate, 16);
        slot.setEngine(int(ShaperEngine::Eq));
        slot.reset();
        const Stereo out = run(slot, x, x, eq);
        return gainDb(out.l, x, n / 2, n);
      };
      const double with = level(makeAllShapers());
      const double without = level(shapersWithoutDcFilter());
      INFO(rate << " Hz host, " << hz << " Hz sine: EQ " << without << " dB, with the DC filter " << with << " dB");
      REQUIRE(std::fabs(with - without) < 0.01);
    }
  }
}

TEST_CASE("A DcBlocker passes audio through unchanged until it is prepared") {
  DcBlocker f;
  std::vector<float> x = sine(64, 440.0);
  x[0] = 0.75f;
  const std::vector<float> in = x;
  f.process(x.data(), int(x.size()));
  REQUIRE(x == in);
}

TEST_CASE("The DC filter starts from rest on restart and on a new prepare") {
  // Fold puts out DC, so the filter holds a large state after a second of it. After restart() (a
  // new note on an idle voice, or a NaN reset), and after prepare() at another rate, the slot must
  // sound exactly like a fresh one.
  const auto x = sine(48000, 110.0, 0.5f);
  ParamValues fold = test::defaultParams(shaperEngineInfo(int(ShaperEngine::RingMod)));
  fold.p[0] = 1.0f / 8.0f;
  fold.p[1] = 0.9f;
  fold.p[2] = 0.8f;
  fold.p[3] = 0.8f;
  auto start = [&](ProcessorSlot& slot) {
    slot.setEngine(int(ShaperEngine::RingMod));
    slot.reset();
  };
  ProcessorSlot used(makeAllShapers());
  used.prepare(kRate, 16);
  start(used);
  used.noteOn(45.0f, 1.0f);
  run(used, x, x, fold);
  used.restart();
  used.noteOn(45.0f, 1.0f);
  ProcessorSlot fresh(makeAllShapers());
  fresh.prepare(kRate, 16);
  start(fresh);
  fresh.noteOn(45.0f, 1.0f);
  const auto shortX = std::vector<float>(x.begin(), x.begin() + 4800);
  const Stereo a = run(used, shortX, shortX, fold);
  const Stereo b = run(fresh, shortX, shortX, fold);
  REQUIRE(a.l == b.l);
  REQUIRE(a.r == b.r);

  run(used, x, x, fold);
  used.prepare(96000.0, 16);
  start(used);
  used.noteOn(45.0f, 1.0f);
  ProcessorSlot fresh96(makeAllShapers());
  fresh96.prepare(96000.0, 16);
  start(fresh96);
  fresh96.noteOn(45.0f, 1.0f);
  const Stereo c = run(used, shortX, shortX, fold);
  const Stereo d = run(fresh96, shortX, shortX, fold);
  REQUIRE(c.l == d.l);
  REQUIRE(c.r == d.r);
}

namespace {

std::vector<SourceSpec> partFxWithoutDcFilter() {
  std::vector<SourceSpec> all = makeAllPartFx();
  for (SourceSpec& s : all) s.dcBlock = false;
  return all;
}

}  // namespace

TEST_CASE("The Part FX's DC filter removes an offset and leaves Off bit-identical") {
  std::vector<float> x = sine(2 * size_t(kRate), 220.0, 0.25f);
  for (float& v : x) v += 0.1f;  // -20 dBFS of DC in the part mix
  auto meanDb = [&](std::vector<SourceSpec> engines, int engine) {
    ProcessorSlot slot(std::move(engines));
    slot.prepare(kRate, 16);
    slot.setEngine(engine);
    slot.reset();
    const Stereo out = run(slot, x, x, test::defaultParams(partFxEngineInfo(engine)));
    const double mean = std::max(std::fabs(meanOf(out.l, size_t(kRate), out.l.size())),
                                 std::fabs(meanOf(out.r, size_t(kRate), out.r.size())));
    return 20.0 * std::log10(mean + 1e-30);
  };
  for (int e = 0; e < kNumPartFxEngines; ++e) {
    if (e == int(PartFxEngine::Off)) continue;
    const double before = meanDb(partFxWithoutDcFilter(), e);
    const double after = meanDb(makeAllPartFx(), e);
    INFO(partFxEngineInfo(e).displayName << ": DC " << before << " dBFS without the filter, " << after << " with it");
    REQUIRE(after < -60.0);
  }
  ParamValues off;
  ProcessorSlot with(makeAllPartFx());
  ProcessorSlot without(partFxWithoutDcFilter());
  with.prepare(kRate, 16);
  without.prepare(kRate, 16);
  const Stereo a = run(with, x, x, off);
  const Stereo b = run(without, x, x, off);
  REQUIRE(a.l == b.l);
  REQUIRE(a.r == b.r);
}
