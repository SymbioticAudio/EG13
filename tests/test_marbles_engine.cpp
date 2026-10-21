#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "alloc_counter.h"
#include "engine/marbles_engine.h"
#include "engine/marbles_scales.h"
#include "marbles/random/random_generator.h"
#include "marbles/random/random_stream.h"
#include "marbles/random/t_generator.h"
#include "marbles/random/x_y_generator.h"
#include "test_helpers.h"

using namespace eg13;

namespace {

ParamValues defaults() { return test::defaultParams(MarblesEngine::staticInfo().engine); }

// A setting away from the defaults: clusters, both deja vu loops on, bump, minor, positive range.
ParamValues busy() {
  ParamValues p = defaults();
  p.p[0] = modOptionValue(1);   // T Model: Clusters
  p.p[1] = modOptionValue(1);   // T Deja Vu on
  p.p[2] = 0.7f;                // T Rate: +40 % (24 semitones)
  p.p[3] = 0.6f;                // T Bias
  p.p[4] = 0.7f;                // Deja Vu
  p.p[5] = modOptionValue(1);   // X Range: Positive
  p.p[6] = 0.3f;                // X Spread
  p.p[7] = 0.6f;                // X Bias
  p.p[8] = 0.8f;                // X Steps
  p.p[9] = modOptionValue(2);   // T Range: 4x
  p.p[10] = 0.2f;               // T Jitter
  p.p[11] = 0.4f;               // Length: round(14) -> 4 steps
  p.p[12] = modOptionValue(1);  // X Mode: Bump
  p.p[13] = modOptionValue(1);  // X Scale: Minor
  p.p[14] = modOptionValue(1);  // X Deja Vu on
  return p;
}

// VCV Marbles' stepBlock (AudibleInstruments Marbles.cpp:440-534) with nothing patched, written
// from that file: the same settings every 5-sample block, T on its internal clock, X/Y clocked
// by T.
class Reference {
 public:
  explicit Reference(uint32_t seed) {
    generator_.Init(seed);
    stream_.Init(&generator_);
    t_.Init(&stream_, 32000.0f);
    xy_.Init(&stream_, 32000.0f);
    for (int i = 0; i < 6; ++i) xy_.LoadScale(i, marbles_scales::preset_scales[i]);
  }
  // out[k]: the 7 outputs of the block's last sample, in volts and gates.
  void block(float out[7]) {
    t_.set_model(marbles::T_GENERATOR_MODEL_CLUSTERS);
    t_.set_range(marbles::T_GENERATOR_RANGE_4X);
    t_.set_rate(60.0f * 0.4f);
    t_.set_bias(0.6f);
    t_.set_jitter(0.2f);
    t_.set_deja_vu(0.7f);
    t_.set_length(4);
    t_.set_pulse_width_mean(0.0f);
    t_.set_pulse_width_std(0.0f);
    float master[5], external[5], slave0[5], slave1[5];
    marbles::Ramps ramps;
    ramps.master = master;
    ramps.external = external;
    ramps.slave[0] = slave0;
    ramps.slave[1] = slave1;
    const stmlib::GateFlags clock[5] = {};
    bool gates[10];
    t_.Process(false, clock, ramps, gates, 5);
    marbles::GroupSettings x;
    x.control_mode = marbles::CONTROL_MODE_BUMP;
    x.voltage_range = marbles::VOLTAGE_RANGE_POSITIVE;
    x.register_mode = false;
    x.register_value = 0.0f;
    x.spread = 0.3f;
    x.bias = 0.6f;
    x.steps = 0.8f;
    x.deja_vu = 0.7f;
    x.scale_index = 1;
    x.length = 4;
    x.ratio.p = 1;
    x.ratio.q = 1;
    marbles::GroupSettings y = x;
    y.control_mode = marbles::CONTROL_MODE_IDENTICAL;
    y.deja_vu = 0.0f;
    y.length = 1;
    y.ratio.q = 4;
    float volts[20];
    xy_.Process(marbles::CLOCK_SOURCE_INTERNAL_T1_T2_T3, x, y, clock, ramps, volts, 5);
    out[0] = gates[8] ? 1.0f : 0.0f;
    out[1] = master[4] < 0.5f ? 1.0f : 0.0f;
    out[2] = gates[9] ? 1.0f : 0.0f;
    for (int k = 0; k < 4; ++k) out[3 + k] = volts[16 + k];
  }

 private:
  marbles::RandomGenerator generator_;
  marbles::RandomStream stream_;
  marbles::TGenerator t_;
  marbles::XYGenerator xy_;
};

std::vector<float> run(uint32_t seed, int ticks) {
  MarblesEngine e;
  e.setSeed(seed);
  e.prepare(48000.0);
  e.reset();
  const ParamValues p = defaults();
  const ModContext ctx;
  std::vector<float> x1;
  float out[kMaxModOutputs];
  for (int t = 0; t < ticks; ++t) {
    e.process(ctx, p, 16, out);
    x1.push_back(out[3]);
  }
  return x1;
}

}  // namespace

TEST_CASE("Random / S&H describes itself") {
  const ModEngineInfo& info = MarblesEngine::staticInfo();
  REQUIRE(std::string(info.engine.id) == "marbles");
  REQUIRE(std::string(info.engine.displayName) == "Random / S&H");
  REQUIRE(info.engine.paramCount == 16);
  REQUIRE(info.outputCount == 7);
  REQUIRE(info.perPartByDefault);
  const char* const outputs[7] = {"T1", "T2", "T3", "X1", "X2", "X3", "Y"};
  for (int k = 0; k < 7; ++k) REQUIRE(std::string(info.outputs[k].name) == outputs[k]);
  char buf[32];
  info.engine.params[5].format(1.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Full");
  info.engine.params[9].format(0.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "1x");
  info.engine.params[13].format(modOptionValue(3), buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Pelog");
  info.engine.params[11].format(0.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "1 steps");
  REQUIRE(MarblesEngine::lengthSteps(1.0f) == 16);
  REQUIRE(MarblesEngine::lengthSteps(0.4f) == 4);
}

TEST_CASE("Random / S&H matches Marbles' generators sample for sample") {
  // Marbles draws random numbers: the adapter and the reference both start from seed 7.
  MarblesEngine e;
  e.setSeed(7);
  e.prepare(MarblesEngine::kNativeRate);
  e.reset();
  const ParamValues p = busy();
  Reference ref(7);
  const ModContext ctx;
  int changes = 0;
  float previous = 0.0f;
  for (int b = 0; b < 64000; ++b) {  // 10 s
    float out[kMaxModOutputs];
    e.process(ctx, p, 5, out);
    float expected[7];
    ref.block(expected);
    INFO("block " << b);
    for (int k = 0; k < 3; ++k) REQUIRE(out[k] == expected[k]);
    for (int k = 3; k < 7; ++k) REQUIRE(out[k] == std::clamp(expected[k] * 0.2f, -1.0f, 1.0f));
    if (out[3] != previous) ++changes;
    previous = out[3];
  }
  REQUIRE(changes > 20);  // X1 really moves
}

TEST_CASE("Each Random / S&H seed plays its own sequence") {
  REQUIRE(run(3, 6000) == run(3, 6000));
  REQUIRE(run(3, 6000) != run(4, 6000));
}

TEST_CASE("Random / S&H X outputs follow X Range") {
  for (int range = 0; range < 3; ++range) {
    MarblesEngine e;
    e.prepare(48000.0);
    e.reset();
    ParamValues p = defaults();
    p.p[5] = modOptionValue(range);
    p.p[6] = 0.9f;  // wide spread
    const ModContext ctx;
    float out[kMaxModOutputs];
    float lowest = 1.0f, highest = -1.0f;
    for (int t = 0; t < 30000; ++t) {
      e.process(ctx, p, 16, out);
      lowest = std::min(lowest, out[3]);
      highest = std::max(highest, out[3]);
    }
    INFO("range " << range);
    REQUIRE(highest <= 1.0f);
    REQUIRE(highest > 0.5f);
    if (range == 2) {
      REQUIRE(lowest < -0.5f);
    } else {
      REQUIRE(lowest >= 0.0f);
    }
  }
}

TEST_CASE("A synced Random / S&H follows the beat") {
  for (double rate : {44100.0, 48000.0, 96000.0}) {
    INFO("host rate " << rate);
    MarblesEngine e;
    e.prepare(rate);
    e.reset();
    ParamValues p = defaults();
    p.p[15] = modOptionValue(1);  // Sync on; T Rate 50 %, 1x: the clock itself
    const double bps = 2.0 / rate;  // 120 BPM
    const int ticksPerSecond = int(std::lround(rate / 16.0));
    float out[kMaxModOutputs];
    bool was = false;
    int edges = 0;
    for (int t = 0; t < 8 * ticksPerSecond; ++t) {  // 8 s: 16 beats
      ModContext ctx;
      ctx.beat = double(t) * 16.0 * bps;
      ctx.beatsPerSample = bps;
      ctx.beatJump = t == 0;
      e.process(ctx, p, 16, out);
      const bool t2 = out[1] > 0.5f;
      if (t >= ticksPerSecond && t2 && !was) ++edges;  // after the clock has locked (2 beats)
      was = t2;
    }
    REQUIRE(edges >= 13);
    REQUIRE(edges <= 15);
  }
}

TEST_CASE("Random / S&H survives NaN and does not allocate") {
  MarblesEngine e;
  e.prepare(48000.0);
  e.reset();
  ParamValues bad;
  for (float& v : bad.p) v = std::numeric_limits<float>::quiet_NaN();
  ModContext ctx;
  float out[kMaxModOutputs];
  for (int t = 0; t < 2000; ++t) {
    e.process(ctx, bad, 16, out);
    for (int k = 0; k < 7; ++k) REQUIRE(std::isfinite(out[k]));
  }
  ParamValues p = busy();
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int t = 0; t < 3000; ++t) {
      ctx.beat = double(t) * 0.01;
      ctx.beatsPerSample = 0.01 / 16.0;
      ctx.beatJump = t % 1000 == 0;
      p.p[15] = modOptionValue((t / 700) % 2);
      e.process(ctx, p, 1 + t % 16, out);
    }
    e.reset();
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}
