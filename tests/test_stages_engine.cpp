#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "alloc_counter.h"
#include "engine/stages_engine.h"
#include "stages/segment_generator.h"
#include "stmlib/utils/gate_flags.h"
#include "test_helpers.h"

using namespace eg13;

namespace {

constexpr int kSamples = 40000;  // 1.28 s at 31250 Hz, a multiple of 16

ParamValues defaults() { return test::defaultParams(StagesEngine::staticInfo().engine); }

// Gate high on native samples [160, 20000): both ends are multiples of 16.
std::vector<bool> gatePattern() {
  std::vector<bool> g(kSamples, false);
  for (int i = 160; i < 20000; ++i) g[size_t(i)] = true;
  return g;
}

// The module's loop for one gated group (stages/stages.cc Process, with the parameter reads of
// VCV's Stages.cpp stepBlock): Configure once, then per 8-sample block set every segment's
// slider (primary) and pot (secondary) and Process.
std::vector<float> reference(const stages::segment::Configuration* config, int count, const ParamValues& p,
                             const std::vector<bool>& gate) {
  static stages::SegmentGenerator g;
  g.Init();
  g.Configure(true, config, count);
  std::vector<float> out(gate.size());
  stmlib::GateFlags previous = stmlib::GATE_FLAG_LOW;
  stages::SegmentGenerator::Output o[8];
  for (size_t b = 0; b < gate.size(); b += 8) {
    stmlib::GateFlags flags[8];
    for (size_t i = 0; i < 8; ++i) previous = flags[i] = stmlib::ExtractGateFlags(previous, gate[b + i]);
    for (int k = 0; k < count; ++k) g.set_segment_parameters(k, p.p[1 + 2 * k], p.p[2 + 2 * k]);
    g.Process(flags, o, 8);
    for (size_t i = 0; i < 8; ++i) out[b + i] = o[i].value;
  }
  return out;
}

// The adapter at the native rate in ticks of n samples; out[t] is the value after tick t.
std::vector<float> render(StagesEngine& e, const ParamValues& p, const std::vector<bool>& gate, int n) {
  std::vector<float> out(gate.size() / size_t(n));
  for (size_t t = 0; t < out.size(); ++t) {
    ModContext ctx;
    ctx.gate = gate[t * size_t(n)];
    float o[kMaxModOutputs];
    e.process(ctx, p, n, o);
    out[t] = o[0];
  }
  return out;
}

ParamValues shaped(int mode) {
  ParamValues p = defaults();
  p.p[0] = modOptionValue(mode);
  const float values[12] = {0.2f, 0.3f, 0.35f, 0.7f, 0.6f, 0.4f, 0.3f, 0.6f, 0.25f, 0.5f, 0.45f, 0.2f};
  for (int i = 0; i < 12; ++i) p.p[1 + i] = values[i];
  return p;
}

void requireMatches(StagesEngine& e, const ParamValues& p, const stages::segment::Configuration* config, int count) {
  const auto gate = gatePattern();
  const auto ref = reference(config, count, p, gate);
  for (int n : {8, 16}) {
    e.reset();
    const auto out = render(e, p, gate, n);
    for (size_t t = 0; t < out.size(); ++t) {
      INFO("tick " << t << " of " << n);
      REQUIRE(out[t] == ref[(t + 1) * size_t(n) - 1]);
    }
  }
}

}  // namespace

TEST_CASE("Envelope describes itself") {
  const ModEngineInfo& info = StagesEngine::staticInfo();
  REQUIRE(std::string(info.engine.id) == "stages");
  REQUIRE(std::string(info.engine.displayName) == "Envelope");
  REQUIRE(info.engine.paramCount == 13);
  REQUIRE(info.outputCount == 1);
  REQUIRE_FALSE(info.outputs[0].bipolar);
  REQUIRE_FALSE(info.perPartByDefault);
  const char* const names[13] = {"Mode", "Attack", "Attack Shape", "Decay", "Decay Shape", "Sustain", "Sustain Time",
                                 "Release", "Release Shape", "Seg 5 A", "Seg 5 B", "Seg 6 A", "Seg 6 B"};
  for (int i = 0; i < 13; ++i) REQUIRE(std::string(info.engine.params[i].name) == names[i]);
  char buf[32];
  const char* const modes[4] = {"ADSR", "AD", "Looping", "Custom"};
  for (int m = 0; m < 4; ++m) {
    info.engine.params[0].format(modOptionValue(m), buf, sizeof(buf));
    REQUIRE(std::string(buf) == modes[m]);
  }
  info.engine.params[0].format(1.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Custom");  // steps past the last option read as the last
  info.engine.params[1].format(0.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "1 ms");
  info.engine.params[3].format(0.5f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "505 ms");
  info.engine.params[7].format(1.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "16.02 s");
}

TEST_CASE("Envelope matches Stages' segment generator sample for sample") {
  using stages::segment::TYPE_HOLD;
  using stages::segment::TYPE_RAMP;
  StagesEngine e;
  e.prepare(StagesEngine::kNativeRate);
  SECTION("ADSR") {
    const stages::segment::Configuration c[4] = {{TYPE_RAMP, false}, {TYPE_RAMP, false}, {TYPE_HOLD, true}, {TYPE_RAMP, false}};
    requireMatches(e, shaped(StagesEngine::Adsr), c, 4);
  }
  SECTION("AD") {
    const stages::segment::Configuration c[2] = {{TYPE_RAMP, false}, {TYPE_RAMP, false}};
    requireMatches(e, shaped(StagesEngine::Ad), c, 2);
  }
  SECTION("Looping") {
    const stages::segment::Configuration c[2] = {{TYPE_RAMP, true}, {TYPE_RAMP, true}};
    requireMatches(e, shaped(StagesEngine::Looping), c, 2);
  }
  SECTION("Custom: ramp, step, hold, ramp, step, ramp") {
    EnvelopeShape s;
    s.count = 6;
    const uint8_t types[6] = {EnvelopeShape::Ramp, EnvelopeShape::Step, EnvelopeShape::Hold,
                              EnvelopeShape::Ramp, EnvelopeShape::Step, EnvelopeShape::Ramp};
    for (int i = 0; i < 6; ++i) {
      s.type[i] = types[i];
      s.loop[i] = i == 2 || i == 3 ? 1 : 0;
    }
    e.setShape(s);
    using stages::segment::TYPE_STEP;
    const stages::segment::Configuration c[6] = {{TYPE_RAMP, false}, {TYPE_STEP, false}, {TYPE_HOLD, true},
                                                 {TYPE_RAMP, true},  {TYPE_STEP, false}, {TYPE_RAMP, false}};
    requireMatches(e, shaped(StagesEngine::Custom), c, 6);
  }
}

TEST_CASE("Envelope sustains at its level and releases to zero") {
  StagesEngine e;
  e.prepare(48000.0);
  e.reset();
  ParamValues p = defaults();
  p.p[5] = 0.6f;  // Sustain
  p.p[7] = 0.3f;  // Release, about 72 ms
  ModContext ctx;
  ctx.gate = true;
  float out[kMaxModOutputs];
  for (int t = 0; t < 3000; ++t) e.process(ctx, p, 16, out);  // 1 s held
  REQUIRE(std::fabs(out[0] - 0.6f) < 1e-3f);
  ctx.gate = false;
  for (int t = 0; t < 3000; ++t) e.process(ctx, p, 16, out);
  REQUIRE(out[0] < 1e-4f);
}

TEST_CASE("Envelope times hold at 44.1, 48 and 96 kHz") {
  auto attackSeconds = [](double rate) {
    StagesEngine e;
    e.prepare(rate);
    e.reset();
    ParamValues p = defaults();
    p.p[0] = modOptionValue(StagesEngine::Ad);
    p.p[1] = 0.3f;  // attack about 72 ms
    ModContext ctx;
    ctx.gate = true;
    float out[kMaxModOutputs] = {};
    int samples = 0;
    while (out[0] < 0.999f && samples < int(rate)) {
      e.process(ctx, p, 16, out);
      samples += 16;
    }
    return double(samples) / rate;
  };
  const double expected = double(StagesEngine::segmentSeconds(0.3f));
  for (double rate : {44100.0, 48000.0, 96000.0}) {
    INFO("rate " << rate);
    REQUIRE(std::fabs(attackSeconds(rate) - expected) < 16.0 / rate + 8.0 / StagesEngine::kNativeRate);
  }
}

TEST_CASE("Changing the Envelope mode while held keeps the output in range") {
  StagesEngine e;
  e.prepare(48000.0);
  e.reset();
  ParamValues p = defaults();
  p.p[5] = 0.5f;
  ModContext ctx;
  ctx.gate = true;
  float out[kMaxModOutputs];
  for (int t = 0; t < 3000; ++t) e.process(ctx, p, 16, out);
  for (int mode : {StagesEngine::Looping, StagesEngine::Ad, StagesEngine::Custom, StagesEngine::Adsr}) {
    p.p[0] = modOptionValue(mode);
    for (int t = 0; t < 2000; ++t) {
      e.process(ctx, p, 16, out);
      REQUIRE(out[0] >= 0.0f);
      REQUIRE(out[0] <= 1.0f);
    }
  }
}

TEST_CASE("Envelope survives NaN and does not allocate") {
  StagesEngine e;
  e.prepare(48000.0);
  e.reset();
  ParamValues bad;
  for (float& v : bad.p) v = std::numeric_limits<float>::quiet_NaN();
  ModContext ctx;
  ctx.gate = true;
  float out[kMaxModOutputs];
  for (int t = 0; t < 500; ++t) {
    e.process(ctx, bad, 16, out);
    REQUIRE(std::isfinite(out[0]));
  }
  ParamValues p = defaults();
  EnvelopeShape custom;
  custom.count = 3;
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int t = 0; t < 2000; ++t) {
      ctx.gate = (t / 100) % 2 == 0;
      ctx.retrigger = t % 300 == 0;
      p.p[0] = modOptionValue(t / 500);
      if (t == 1600) e.setShape(custom);
      e.process(ctx, p, 1 + t % 16, out);
    }
    e.reset();
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}
