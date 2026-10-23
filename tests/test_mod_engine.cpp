#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>

#include "alloc_counter.h"
#include "engine/mod_engine.h"
#include "test_mod_engines.h"

using namespace eg13;

namespace {

float tick(NativeModEngine& e, const ModContext& ctx, int n) {
  float out[kMaxModOutputs];
  e.process(ctx, ParamValues{}, n, out);
  return out[0];
}

}  // namespace

TEST_CASE("At the native rate the sampler reports the last sample of each tick") {
  test::CountingModEngine e(31250.0, 8);
  e.prepare(31250.0);
  e.reset();
  const ModContext ctx;
  for (int t = 0; t < 10; ++t) REQUIRE(tick(e, ctx, 16) == float(16 * t + 15));
  REQUIRE(e.renderedSamples() == 160);
  REQUIRE(e.blocks == 20);
}

TEST_CASE("Short ticks hold, then report the samples whose time has come") {
  test::CountingModEngine e(31250.0, 8);
  e.prepare(31250.0);
  e.reset();
  const ModContext ctx;
  REQUIRE(tick(e, ctx, 3) == 2.0f);
  REQUIRE(e.renderedSamples() == 8);
  REQUIRE(tick(e, ctx, 3) == 5.0f);
  REQUIRE(e.renderedSamples() == 8);  // still inside the first block
  REQUIRE(tick(e, ctx, 3) == 8.0f);
  REQUIRE(e.renderedSamples() == 16);
}

TEST_CASE("The sampler keeps native time at every host rate") {
  struct Engine {
    double rate;
    int block;
  };
  for (const Engine engine : {Engine{31250.0, 8}, Engine{62500.0, 8}, Engine{32000.0, 5}, Engine{48000.0, 4}}) {
    for (double hostRate : {44100.0, 48000.0, 96000.0, 176400.0}) {
      test::CountingModEngine e(engine.rate, engine.block);
      e.prepare(hostRate);
      e.reset();
      const ModContext ctx;
      float last = 0.0f;
      const int total = int(hostRate);  // one second
      for (int done = 0; done < total; done += 16) last = tick(e, ctx, std::min(16, total - done));
      INFO("native " << engine.rate << " host " << hostRate);
      REQUIRE(last == float(engine.rate - 1.0));
      REQUIRE(e.renderedSamples() >= int64_t(engine.rate));
      REQUIRE(e.renderedSamples() < int64_t(engine.rate) + engine.block);
    }
  }
}

TEST_CASE("A host rate that rounds to 0 mHz runs the grid 1:1 instead of dividing by zero") {
  for (double hostRate : {0.0004, -1.0, 3.0e6}) {
    INFO(hostRate);
    test::CountingModEngine e(31250.0, 8);
    e.prepare(hostRate);
    e.reset();
    const ModContext ctx;
    REQUIRE(tick(e, ctx, 16) == 15.0f);
  }
}

TEST_CASE("The sampler does not drift over an hour") {
  // An independent reference: the native samples due after h host samples are exactly
  // floor(h * native / host), computed here in integers.
  for (const int64_t hostRate : {int64_t(44100), int64_t(176400)}) {
    test::CountingModEngine e(32000.0, 5);
    e.prepare(double(hostRate));
    e.reset();
    const ModContext ctx;
    float out[kMaxModOutputs];
    const int64_t total = 3600 * hostRate;
    int64_t done = 0;
    bool inStep = true;
    while (done < total) {
      const int n = int(std::min<int64_t>(4099, total - done));  // odd ticks, to cross every phase
      e.process(ctx, ParamValues{}, n, out);
      done += n;
      const int64_t due = done * 32000 / hostRate;
      inStep = inStep && e.renderedSamples() >= due && e.renderedSamples() < due + 5;
    }
    INFO("host " << hostRate);
    REQUIRE(inStep);
    REQUIRE(e.renderedSamples() == 3600 * 32000);  // whole blocks: 32000 / 5 per second
  }
}

TEST_CASE("Gate edges reach the engine at its next block") {
  test::CountingModEngine e(31250.0, 8);
  e.prepare(31250.0);
  e.reset();
  ModContext ctx;
  tick(e, ctx, 8);
  for (uint8_t g : e.lastGates) REQUIRE(g == kGateLow);
  ctx.gate = true;
  tick(e, ctx, 8);
  REQUIRE(e.lastGates[0] == (kGateRising | kGateHigh));
  for (int i = 1; i < 8; ++i) REQUIRE(e.lastGates[i] == kGateHigh);
  ctx.retrigger = true;  // a new note while the gate is high
  tick(e, ctx, 8);
  REQUIRE(e.lastGates[0] == (kGateRising | kGateHigh));
  REQUIRE(e.lastGates[1] == kGateHigh);
  ctx.retrigger = false;
  ctx.gate = false;
  tick(e, ctx, 8);
  REQUIRE(e.lastGates[0] == kGateFalling);
  REQUIRE(e.lastGates[1] == kGateLow);
  ctx.retrigger = true;  // a note that started and ended within one tick
  tick(e, ctx, 8);
  REQUIRE(e.lastGates[0] == (kGateRising | kGateHigh));
  REQUIRE(e.lastGates[1] == kGateFalling);
  REQUIRE(e.lastGates[2] == kGateLow);
}

TEST_CASE("A retrigger waits for the next rendered block") {
  test::CountingModEngine e(31250.0, 8);
  e.prepare(48000.0);
  e.reset();
  ModContext ctx;
  ctx.gate = true;
  ctx.retrigger = true;
  tick(e, ctx, 1);  // 0.65 native samples: nothing rendered yet
  REQUIRE(e.blocks == 0);
  ctx.retrigger = false;
  tick(e, ctx, 1);
  REQUIRE(e.blocks == 1);
  REQUIRE(e.lastGates[0] == (kGateRising | kGateHigh));
}

TEST_CASE("Beat positions follow the host's tempo per native sample") {
  test::CountingModEngine e(32000.0, 5);
  e.prepare(48000.0);
  e.reset();
  ModContext ctx;
  ctx.beat = 10.0;
  ctx.beatsPerSample = 2.0 / 48000.0;  // 120 BPM
  ctx.beatJump = true;
  tick(e, ctx, 16);  // due: floor(16 * 2 / 3) = 10 native samples, two blocks
  REQUIRE(e.blocks == 2);
  REQUIRE_FALSE(e.lastJump);  // the jump went to the first block only
  const double perNative = 2.0 / 32000.0;
  for (int i = 0; i < 5; ++i) REQUIRE(std::fabs(e.lastBeats[i] - (10.0 + double(5 + i) * perNative)) < 1e-12);
}

TEST_CASE("Reset starts the sampler over") {
  test::CountingModEngine e(31250.0, 8);
  e.prepare(31250.0);
  e.reset();
  const ModContext ctx;
  for (int t = 0; t < 5; ++t) tick(e, ctx, 16);
  e.reset();
  REQUIRE(e.renderedSamples() == 0);
  REQUIRE(tick(e, ctx, 16) == 15.0f);
}

TEST_CASE("The Off mod engine outputs zeros") {
  OffModEngine off;
  float out[kMaxModOutputs];
  std::fill(std::begin(out), std::end(out), 1.0f);
  off.process(ModContext{}, ParamValues{}, 16, out);
  for (float v : out) REQUIRE(v == 0.0f);
  REQUIRE(off.info().outputCount == 0);
}

TEST_CASE("Envelope shapes are sanitized") {
  EnvelopeShape s;
  s.count = 0;
  s.type[1] = 9;
  s.loop[2] = 5;
  const EnvelopeShape clean = sanitized(s);
  REQUIRE(clean.count == 1);
  REQUIRE(clean.type[1] == EnvelopeShape::Ramp);
  REQUIRE(clean.loop[2] == 1);
  s.count = 40;
  REQUIRE(sanitized(s).count == EnvelopeShape::kMaxSegments);
  REQUIRE(sanitized(EnvelopeShape{}) == EnvelopeShape{});
}

TEST_CASE("Mod option steps are fixed") {
  REQUIRE(modOption(0.0f, 4) == 0);
  REQUIRE(modOption(modOptionValue(2), 4) == 2);
  REQUIRE(modOption(1.0f, 4) == 3);  // past the last option: the last option
  REQUIRE(modOption(modOptionValue(3), 3) == 2);
}

TEST_CASE("The sampler does not allocate") {
  test::CountingModEngine e(62500.0, 8);
  e.prepare(44100.0);
  ModContext ctx;
  float out[kMaxModOutputs];
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    e.reset();
    for (int t = 0; t < 1000; ++t) {
      ctx.gate = (t / 50) % 2 == 0;
      e.process(ctx, ParamValues{}, 1 + t % 16, out);
    }
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}

TEST_CASE("Mod option names follow the fixed steps") {
  const char* const names[3] = {"One", "Two", "Three"};
  char buf[16];
  formatOption(names, 3, 0.0f, buf, sizeof buf);
  REQUIRE(std::strcmp(buf, "One") == 0);
  formatOption(names, 3, modOptionValue(1), buf, sizeof buf);
  REQUIRE(std::strcmp(buf, "Two") == 0);
  formatOption(names, 3, 1.0f, buf, sizeof buf);  // past the last option: the last option
  REQUIRE(std::strcmp(buf, "Three") == 0);
}
