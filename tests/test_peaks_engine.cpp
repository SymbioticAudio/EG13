#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "alloc_counter.h"
#include "engine/peaks_engine.h"
#include "peaks/gate_processor.h"
#include "peaks/modulations/lfo.h"
#include "peaks/modulations/multistage_envelope.h"
#include "stmlib/utils/random.h"
#include "test_helpers.h"

using namespace eg13;

namespace {

ParamValues defaults() { return test::defaultParams(PeaksEngine::staticInfo().engine); }

bool gateAt(int block) { return (block / 2000) % 2 == 1; }  // 4-sample blocks: 1/6 s on, 1/6 s off

// The adapter at 48 kHz in ticks of n samples (n a multiple of 4); out[t] after tick t.
std::vector<float> render(const ParamValues& p, int n, int ticks) {
  PeaksEngine e;
  e.prepare(PeaksEngine::kNativeRate);
  e.reset();
  std::vector<float> out;
  for (int t = 0; t < ticks; ++t) {
    ModContext ctx;
    ctx.gate = gateAt(t * n / 4);
    float o[kMaxModOutputs];
    e.process(ctx, p, n, o);
    out.push_back(o[0]);
  }
  return out;
}

// peaks/peaks.cc Process for one channel in full control mode, written from the firmware: the
// pots reach the processor through Configure(), then 4-sample blocks of Process(). Returns the
// last sample of every block.
template <typename Processor>
std::vector<int16_t> reference(Processor& processor, const uint16_t knobs[4], int blocks) {
  std::vector<int16_t> out;
  peaks::GateFlags previous = peaks::GATE_FLAG_LOW;
  uint16_t k[4];
  for (int b = 0; b < blocks; ++b) {
    std::copy(knobs, knobs + 4, k);
    processor.Configure(k, peaks::CONTROL_MODE_FULL);
    peaks::GateFlags flags[4];
    for (auto& f : flags) previous = f = peaks::ExtractGateFlags(previous, gateAt(b));
    int16_t o[4];
    processor.Process(flags, o, 4);
    out.push_back(o[3]);
  }
  return out;
}

}  // namespace

TEST_CASE("Function Generator describes itself") {
  const ModEngineInfo& info = PeaksEngine::staticInfo();
  REQUIRE(std::string(info.engine.id) == "peaks");
  REQUIRE(std::string(info.engine.displayName) == "Function Generator");
  REQUIRE(info.engine.paramCount == 11);
  REQUIRE(info.outputCount == 1);
  REQUIRE_FALSE(info.perPartByDefault);
  const char* const names[11] = {"Mode",    "Attack", "Rate",    "Decay",      "Wave Shape", "Sustain",
                                 "Phase",   "Release", "Smoothness", "(unused)", "Waveform"};
  for (int i = 0; i < 11; ++i) REQUIRE(std::string(info.engine.params[i].name) == names[i]);
  char buf[32];
  info.engine.params[0].format(modOptionValue(2), buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Tap LFO");
  info.engine.params[10].format(modOptionValue(4), buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Noise");
  info.engine.params[2].format(0.5f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "2.24 Hz");
  for (int w = 0; w < PeaksEngine::kNumWaveforms; ++w) {
    REQUIRE((uint32_t(PeaksEngine::waveformValue(w)) * peaks::LFO_SHAPE_LAST >> 16) == uint32_t(w));
  }
  REQUIRE(PeaksEngine::knob(1.0f) == 65535);
}

TEST_CASE("Function Generator's Envelope matches Peaks sample for sample") {
  ParamValues p = defaults();
  p.p[1] = 0.3f;
  p.p[3] = 0.45f;
  p.p[5] = 0.6f;
  p.p[7] = 0.4f;
  uint16_t k[4];
  PeaksEngine::knobs(PeaksEngine::Envelope, p, k);
  static peaks::MultistageEnvelope env;  // the firmware's processors are zeroed globals
  std::memset(static_cast<void*>(&env), 0, sizeof(env));
  env.Init();
  const auto ref = reference(env, k, 24000);  // 2 s
  for (int n : {4, 16}) {
    const auto out = render(p, n, 24000 * 4 / n);
    for (size_t t = 0; t < out.size(); ++t) {
      INFO("tick " << t << " of " << n);
      REQUIRE(out[t] == float(ref[(t + 1) * size_t(n) / 4 - 1]) / 32767.0f);
    }
  }
}

TEST_CASE("Function Generator's LFO matches Peaks sample for sample") {
  for (int waveform = 0; waveform < 4; ++waveform) {
    ParamValues p = defaults();
    p.p[0] = modOptionValue(PeaksEngine::Lfo);
    p.p[2] = 0.6f;
    p.p[4] = 0.3f;
    p.p[6] = 0.7f;
    p.p[10] = modOptionValue(waveform);
    uint16_t k[4];
    PeaksEngine::knobs(PeaksEngine::Lfo, p, k);
    static peaks::Lfo lfo;
    std::memset(static_cast<void*>(&lfo), 0, sizeof(lfo));
    lfo.Init();
    const auto ref = reference(lfo, k, 24000);
    const auto out = render(p, 4, 24000);
    for (size_t t = 0; t < out.size(); ++t) {
      INFO("waveform " << waveform << " block " << t);
      REQUIRE(out[t] == std::clamp(float(ref[t]) / 20480.0f, -1.0f, 1.0f));
    }
  }
}

TEST_CASE("Function Generator's Random matches Peaks' noise LFO") {
  ParamValues p = defaults();
  p.p[0] = modOptionValue(PeaksEngine::Random);
  p.p[2] = 0.7f;
  p.p[8] = 0.8f;
  stmlib::Random::Seed(42);
  const auto out = render(p, 4, 24000);
  uint16_t k[4];
  PeaksEngine::knobs(PeaksEngine::Random, p, k);
  static peaks::Lfo lfo;
  std::memset(static_cast<void*>(&lfo), 0, sizeof(lfo));
  lfo.Init();
  stmlib::Random::Seed(42);
  const auto ref = reference(lfo, k, 24000);
  for (size_t t = 0; t < out.size(); ++t) REQUIRE(out[t] == std::clamp(float(ref[t]) / 20480.0f, -1.0f, 1.0f));
  REQUIRE(*std::max_element(out.begin(), out.end()) - *std::min_element(out.begin(), out.end()) > 0.5f);
}

TEST_CASE("Function Generator's Tap LFO follows the notes' tempo") {
  // Each tap restarts the cycle (Lfo::Process), so counting cycles alone cannot see the tempo:
  // where in the tap interval the wave crosses zero can. At the tapped tempo that point is the
  // same fraction of the interval at every tempo.
  for (const double rate : {44100.0, 48000.0, 96000.0}) {
    double fraction[2] = {};
    int index = 0;
    for (const double seconds : {0.5, 0.3}) {  // 120 and 200 BPM
      INFO("host rate " << rate << ", a tap every " << seconds << " s");
      PeaksEngine e;
      e.prepare(rate);
      e.reset();
      ParamValues p = defaults();
      p.p[0] = modOptionValue(PeaksEngine::TapLfo);
      p.p[10] = modOptionValue(1);  // triangle
      float out[kMaxModOutputs];
      float previous = 0.0f;
      int rising = 0;
      double first = -1.0, lowest = 1.0, highest = 0.0;
      const double tap = rate * seconds;
      const int ticks = int(rate * 8.0 / 16.0);  // 8 s
      for (int t = 0; t < ticks; ++t) {
        const double start = double(t) * 16.0;
        // The latest tap that has begun by the end of this tick; the gate is high from the tick
        // that holds it for about 2 ms, with the retrigger on that first tick.
        const double tapTime = std::floor((start + 15.0) / tap) * tap;
        const double tapTick = std::floor(tapTime / 16.0) * 16.0;
        ModContext ctx;
        ctx.retrigger = start == tapTick;
        ctx.gate = start - tapTick < 0.0021 * rate;
        e.process(ctx, p, 16, out);
        if (start >= 2.0 * rate && previous < 0.0f && out[0] >= 0.0f) {  // after the taps are learnt
          ++rising;
          const double f = (start - tapTick) / tap;
          if (first < 0.0) first = f;
          lowest = std::min(lowest, f);
          highest = std::max(highest, f);
        }
        previous = out[0];
      }
      const int taps = int(6.0 / seconds);
      REQUIRE(rising >= taps - 1);  // one cycle per tap
      REQUIRE(rising <= taps + 1);
      REQUIRE(highest - lowest < 0.01);  // always at the same point of the interval
      fraction[index++] = first;
    }
    REQUIRE(std::fabs(fraction[0] - fraction[1]) < 0.01);  // the cycle scales with the tempo
  }
}

TEST_CASE("Function Generator's Envelope sustains fully by default and releases") {
  PeaksEngine e;
  e.prepare(44100.0);
  e.reset();
  const ParamValues p = defaults();
  ModContext ctx;
  ctx.gate = true;
  float out[kMaxModOutputs];
  for (int t = 0; t < 4000; ++t) e.process(ctx, p, 16, out);
  REQUIRE(out[0] > 0.999f);
  ctx.gate = false;
  for (int t = 0; t < 8000; ++t) e.process(ctx, p, 16, out);
  REQUIRE(out[0] < 0.001f);
}

TEST_CASE("Function Generator survives NaN and does not allocate") {
  PeaksEngine e;
  e.prepare(96000.0);
  e.reset();
  ParamValues bad;
  for (float& v : bad.p) v = std::numeric_limits<float>::quiet_NaN();
  ModContext ctx;
  float out[kMaxModOutputs];
  for (int t = 0; t < 500; ++t) {
    e.process(ctx, bad, 16, out);
    REQUIRE(std::isfinite(out[0]));
  }
  ParamValues p = defaults();
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int t = 0; t < 4000; ++t) {
      ctx.gate = (t / 100) % 2 == 0;
      ctx.retrigger = t % 100 == 0;
      p.p[0] = modOptionValue((t / 500) % 4);
      e.process(ctx, p, 1 + t % 16, out);
    }
    e.reset();
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}

TEST_CASE("A reset Function Generator starts from the same state every time") {
  PeaksEngine e;
  e.prepare(48000.0);
  ParamValues p = defaults();
  p.p[0] = modOptionValue(PeaksEngine::Lfo);
  auto run = [&]() {
    e.reset();
    std::vector<float> values;
    float out[kMaxModOutputs];
    for (int t = 0; t < 1000; ++t) {
      e.process(ModContext{}, p, 16, out);
      values.push_back(out[0]);
    }
    return values;
  };
  const auto first = run();
  REQUIRE(run() == first);
}

TEST_CASE("Function Generator's pot mapping is pinned for every mode") {
  // The sample-for-sample references take their pot values from knobs(); this pins that mapping.
  // 0, 0.25, 0.5 and 1 give the pots 0, 16384, 32768 and 65535 exactly (0.5 and 0.25 round up).
  REQUIRE(PeaksEngine::knob(0.0f) == 0);
  REQUIRE(PeaksEngine::knob(0.25f) == 16384);
  REQUIRE(PeaksEngine::knob(0.5f) == 32768);
  REQUIRE(PeaksEngine::knob(1.0f) == 65535);
  ParamValues p = defaults();
  p.p[1] = 0.0f;    // Attack
  p.p[2] = 1.0f;    // Rate
  p.p[3] = 0.5f;    // Decay
  p.p[4] = 0.25f;   // Wave Shape
  p.p[5] = 1.0f;    // Sustain
  p.p[6] = 0.5f;    // Phase
  p.p[7] = 0.25f;   // Release
  p.p[8] = 0.0f;    // Smoothness
  p.p[10] = modOptionValue(1);  // Triangle
  uint16_t k[4];
  PeaksEngine::knobs(PeaksEngine::Envelope, p, k);
  REQUIRE(k[0] == 0);
  REQUIRE(k[1] == 32768);
  REQUIRE(k[2] == 65535);
  REQUIRE(k[3] == 16384);
  PeaksEngine::knobs(PeaksEngine::Lfo, p, k);
  REQUIRE(k[0] == 65535);
  REQUIRE(k[1] == 19660);  // Triangle: (1 * 65536 + 32768) / 5
  REQUIRE(k[2] == 16384);
  REQUIRE(k[3] == 32768);
  PeaksEngine::knobs(PeaksEngine::TapLfo, p, k);
  REQUIRE(k[0] == 40960);
  REQUIRE(k[1] == 19660);
  REQUIRE(k[2] == 16384);
  REQUIRE(k[3] == 32768);
  PeaksEngine::knobs(PeaksEngine::Random, p, k);
  REQUIRE(k[0] == 65535);
  REQUIRE(k[1] == 58982);  // Noise: (4 * 65536 + 32768) / 5
  REQUIRE(k[2] == 0);
  REQUIRE(k[3] == 32768);
  const uint16_t waves[5] = {6553, 19660, 32768, 45875, 58982};
  for (int w = 0; w < 5; ++w) REQUIRE(PeaksEngine::waveformValue(w) == waves[w]);
}
