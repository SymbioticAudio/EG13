#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

#include "marbles/random/random_generator.h"
#include "marbles/random/random_stream.h"
#include "marbles/random/t_generator.h"
#include "marbles/random/x_y_generator.h"
#include "peaks/modulations/lfo.h"
#include "stages/segment_generator.h"
#include "stmlib/utils/gate_flags.h"
#include "tides2/poly_slope_generator.h"

TEST_CASE("Upstream Stages runs an ADSR") {
  static stages::SegmentGenerator g;  // about 4 KB: keep it off the stack
  g.Init();
  const stages::segment::Configuration adsr[4] = {{stages::segment::TYPE_RAMP, false},
                                                  {stages::segment::TYPE_RAMP, false},
                                                  {stages::segment::TYPE_HOLD, true},
                                                  {stages::segment::TYPE_RAMP, false}};
  g.Configure(true, adsr, 4);
  g.set_segment_parameters(0, 0.2f, 0.5f);  // attack about 22 ms
  g.set_segment_parameters(1, 0.3f, 0.5f);  // decay about 72 ms
  g.set_segment_parameters(2, 0.6f, 0.5f);  // sustain level 0.6
  g.set_segment_parameters(3, 0.3f, 0.5f);  // release about 72 ms
  stmlib::GateFlags flags[8];
  stages::SegmentGenerator::Output out[8];
  stmlib::GateFlags previous = stmlib::GATE_FLAG_LOW;
  float held = 0.0f;
  float peak = 0.0f;
  for (int block = 0; block < 3906; ++block) {  // 1 s at 31250 Hz; gate high for the first half
    const bool gate = block < 1953;
    for (auto& f : flags) previous = f = stmlib::ExtractGateFlags(previous, gate);
    g.Process(flags, out, 8);
    peak = std::max(peak, out[7].value);
    if (block == 1950) held = out[7].value;
  }
  REQUIRE(peak > 0.99f);
  REQUIRE(std::fabs(held - 0.6f) < 0.01f);
  REQUIRE(out[7].value < 0.01f);
}

TEST_CASE("Upstream Tides 2 cycles at its frequency") {
  static tides::PolySlopeGenerator g;
  g.Init();
  const stmlib::GateFlags noGate[4] = {};
  tides::PolySlopeGenerator::OutputSample out[4];
  // Slope/phase output runs at half rate, as tides2/tides.cc:142-152 does: 4 samples per 8-sample
  // block at twice the frequency. 2 Hz for 1 s.
  const float frequency = 2.0f * 2.0f / 62500.0f;
  float lowest = 0.0f, highest = 0.0f, previous = 0.0f;
  int rising = 0;
  for (int block = 0; block < 62500 / 8; ++block) {
    g.Render(tides::RAMP_MODE_LOOPING, tides::OUTPUT_MODE_SLOPE_PHASE, tides::RANGE_CONTROL, frequency, 0.5f,
             0.5f, 0.5f, 0.5f, noGate, nullptr, out, 4);
    for (const auto& s : out) {
      const float v = s.channel[0];
      lowest = std::min(lowest, v);
      highest = std::max(highest, v);
      if (previous < 0.0f && v >= 0.0f) ++rising;
      previous = v;
    }
  }
  REQUIRE(lowest < -4.0f);  // +-5 V when cycling
  REQUIRE(highest > 4.0f);
  REQUIRE(rising >= 1);
  REQUIRE(rising <= 3);
}

TEST_CASE("Upstream Marbles makes gates and voltages") {
  static marbles::RandomGenerator generator;
  static marbles::RandomStream stream;
  static marbles::TGenerator t;
  static marbles::XYGenerator xy;
  generator.Init(1);
  stream.Init(&generator);
  t.Init(&stream, 32000.0f);
  xy.Init(&stream, 32000.0f);
  t.set_model(marbles::T_GENERATOR_MODEL_COMPLEMENTARY_BERNOULLI);
  t.set_range(marbles::T_GENERATOR_RANGE_1X);
  t.set_rate(0.0f);
  t.set_bias(0.5f);
  t.set_jitter(0.0f);
  t.set_deja_vu(0.0f);
  t.set_length(1);
  t.set_pulse_width_mean(0.0f);
  t.set_pulse_width_std(0.0f);
  marbles::GroupSettings x{};
  x.control_mode = marbles::CONTROL_MODE_IDENTICAL;
  x.voltage_range = marbles::VOLTAGE_RANGE_FULL;
  x.spread = 0.5f;
  x.bias = 0.5f;
  x.steps = 0.5f;
  x.length = 1;
  x.ratio.p = 1;
  x.ratio.q = 1;
  marbles::GroupSettings y = x;
  y.ratio.q = 4;
  float master[5], external[5], slave0[5], slave1[5];
  marbles::Ramps ramps;
  ramps.master = master;
  ramps.external = external;
  ramps.slave[0] = slave0;
  ramps.slave[1] = slave1;
  bool gates[10];
  float volts[20];
  const stmlib::GateFlags clock[5] = {};
  int edges = 0;
  bool was = false;
  float lowest = 0.0f, highest = 0.0f;
  for (int block = 0; block < 2 * 32000 / 5; ++block) {  // 2 s
    t.Process(false, clock, ramps, gates, 5);
    xy.Process(marbles::CLOCK_SOURCE_INTERNAL_T1_T2_T3, x, y, clock, ramps, volts, 5);
    for (int i = 0; i < 5; ++i) {
      const bool t2 = master[i] < 0.5f;
      if (t2 && !was) ++edges;
      was = t2;
      lowest = std::min(lowest, volts[4 * i]);
      highest = std::max(highest, volts[4 * i]);
    }
  }
  REQUIRE(edges >= 2);  // T2 is the clock itself: about 2 Hz at rate 0
  REQUIRE(lowest >= -5.01f);
  REQUIRE(highest <= 5.01f);
  REQUIRE(highest - lowest > 1.0f);
}

TEST_CASE("Upstream Peaks runs an LFO") {
  static peaks::Lfo lfo;
  lfo.Init();
  uint16_t knobs[4] = {32768, 6553, 32768, 32768};  // 2.2 Hz, sine, neutral shape, phase 0
  lfo.Configure(knobs, peaks::CONTROL_MODE_FULL);
  const peaks::GateFlags gates[4] = {};
  int16_t out[4];
  int lowest = 0, highest = 0;
  for (int block = 0; block < 48000 / 4; ++block) {
    lfo.Process(gates, out, 4);
    for (int16_t v : out) {
      lowest = std::min(lowest, int(v));
      highest = std::max(highest, int(v));
    }
  }
  REQUIRE(lowest < -15000);
  REQUIRE(highest > 15000);
}
