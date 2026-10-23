#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <string>
#include <vector>

#include "alloc_counter.h"
#include "engine/tides_engine.h"
#include "stmlib/dsp/units.h"
#include "stmlib/utils/gate_flags.h"
#include "test_helpers.h"
#include "tides2/poly_slope_generator.h"

using namespace eg13;

namespace {

ParamValues defaults() { return test::defaultParams(TidesEngine::staticInfo().engine); }

// tides2/tides.cc Process for one 8-sample block with TRIG patched and CLOCK unpatched, written
// from the firmware: the range root and transposition give the frequency, the slope/phase and
// frequency output modes run at half rate on the even gate samples, and a change of output mode
// resets the generator.
class Reference {
 public:
  Reference() { g_.Init(); }
  void block(tides::RampMode mode, tides::OutputMode output, float rootHz, float transposition, float slope,
             float shape, float smoothness, float shift, const stmlib::GateFlags* gate, float (*out)[8]) {
    const bool half = output >= tides::OUTPUT_MODE_SLOPE_PHASE;
    size_t size = 8;
    stmlib::GateFlags flags[8];
    std::copy(gate, gate + 8, flags);
    float frequency = rootHz / 62500.0f * stmlib::SemitonesToRatio(transposition);
    if (half) {
      size = 4;
      for (size_t i = 0; i < 4; ++i) flags[i] = flags[2 * i];
      frequency *= 2.0f;
    }
    if (first_ || output != previous_) {
      g_.Reset();
      previous_ = output;
      first_ = false;
    }
    tides::PolySlopeGenerator::OutputSample o[8];
    g_.Render(mode, output, tides::RANGE_CONTROL, frequency, slope, shape, smoothness, shift, flags, nullptr, o, size);
    for (size_t i = 0; i < 8; ++i) {
      for (int k = 0; k < 4; ++k) out[k][i] = o[half ? i / 2 : i].channel[k];
    }
  }

 private:
  tides::PolySlopeGenerator g_;
  tides::OutputMode previous_ = tides::OUTPUT_MODE_GATES;
  bool first_ = true;
};

struct Setup {
  int mode;
  tides::RampMode rampMode;
  int output;
  tides::OutputMode outputMode;
};

void requireMatches(const Setup& setup) {
  TidesEngine e;
  e.prepare(TidesEngine::kNativeRate);
  e.reset();
  ParamValues p = defaults();
  p.p[0] = modOptionValue(setup.mode);
  p.p[2] = 0.62f;  // +11.5 semitones
  p.p[3] = 0.3f;   // shape
  p.p[4] = 0.7f;   // slope
  p.p[5] = 0.4f;   // shift
  p.p[6] = 0.35f;  // smoothness (low-pass)
  p.p[9] = modOptionValue(setup.output);
  Reference ref;
  stmlib::GateFlags previous = stmlib::GATE_FLAG_LOW;
  for (int b = 0; b < 8000; ++b) {  // about 1 s
    const bool gate = (b / 600) % 2 == 1;
    ModContext ctx;
    ctx.gate = gate;
    float out[kMaxModOutputs];
    e.process(ctx, p, 8, out);
    stmlib::GateFlags flags[8];
    for (auto& f : flags) previous = f = stmlib::ExtractGateFlags(previous, gate);
    float expected[4][8];
    ref.block(setup.rampMode, setup.outputMode, 2.0f, (0.62f - 0.5f) * 96.0f, 0.7f, 0.3f, 0.35f, 0.4f,
              flags, expected);
    const float scale = setup.rampMode == tides::RAMP_MODE_LOOPING ? 0.2f : 0.125f;
    for (int k = 0; k < 4; ++k) {
      INFO("block " << b << " output " << k);
      REQUIRE(out[k] == std::clamp(expected[k][7] * scale, -1.0f, 1.0f));
    }
  }
}

// Rising zero crossings of output 1 over `seconds` of free running at `rate`.
int cycles(double rate, double seconds, const ParamValues& p) {
  TidesEngine e;
  e.prepare(rate);
  e.reset();
  const ModContext ctx;
  float out[kMaxModOutputs];
  float previous = 0.0f;
  int rising = 0;
  const int total = int(rate * seconds);
  for (int done = 0; done < total; done += 16) {
    e.process(ctx, p, std::min(16, total - done), out);
    if (previous < 0.0f && out[0] >= 0.0f) ++rising;
    previous = out[0];
  }
  return rising;
}

}  // namespace

TEST_CASE("LFO describes itself") {
  const ModEngineInfo& info = TidesEngine::staticInfo();
  REQUIRE(std::string(info.engine.id) == "tides");
  REQUIRE(std::string(info.engine.displayName) == "LFO");
  REQUIRE(info.engine.paramCount == 11);
  REQUIRE(info.outputCount == 4);
  REQUIRE_FALSE(info.perPartByDefault);
  const char* const names[11] = {"Mode", "Sync", "Frequency", "Shape", "Slope", "Shift", "Smoothness",
                                 "(unused)", "(unused)", "Output", "Range"};
  for (int i = 0; i < 11; ++i) REQUIRE(std::string(info.engine.params[i].name) == names[i]);
  REQUIRE(info.engine.params[7].format == nullptr);
  char buf[32];
  info.engine.params[0].format(0.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Cycle");
  info.engine.params[9].format(0.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Phase");
  info.engine.params[10].format(0.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Medium");
  info.engine.params[2].format(0.75f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "+24.0 st");
  REQUIRE(std::fabs(TidesEngine::frequencyFor(5.0, TidesEngine::Medium) - 0.66524f) < 1e-4f);
}

TEST_CASE("LFO matches Tides 2's slope generator sample for sample") {
  SECTION("Cycle, phase outputs (half rate)") {
    requireMatches({TidesEngine::Cycle, tides::RAMP_MODE_LOOPING, TidesEngine::Phase, tides::OUTPUT_MODE_SLOPE_PHASE});
  }
  SECTION("Cycle, gates outputs (full rate)") {
    requireMatches({TidesEngine::Cycle, tides::RAMP_MODE_LOOPING, TidesEngine::Gates, tides::OUTPUT_MODE_GATES});
  }
  SECTION("AD, amplitude outputs") {
    requireMatches({TidesEngine::Ad, tides::RAMP_MODE_AD, TidesEngine::Amplitude, tides::OUTPUT_MODE_AMPLITUDE});
  }
  SECTION("AR, frequency outputs") {
    requireMatches({TidesEngine::Ar, tides::RAMP_MODE_AR, TidesEngine::Frequency, tides::OUTPUT_MODE_FREQUENCY});
  }
}

TEST_CASE("LFO frequency holds at 44.1, 48 and 96 kHz") {
  const ParamValues p = defaults();  // 2 Hz
  for (double rate : {44100.0, 48000.0, 96000.0}) {
    INFO("rate " << rate);
    const int n = cycles(rate, 10.0, p);
    REQUIRE(n >= 19);
    REQUIRE(n <= 21);
  }
  ParamValues low = p;
  low.p[10] = modOptionValue(TidesEngine::Low);
  low.p[2] = TidesEngine::frequencyFor(1.0, TidesEngine::Low);
  const int n = cycles(48000.0, 10.0, low);
  REQUIRE(n >= 9);
  REQUIRE(n <= 11);
}

TEST_CASE("LFO outputs stay in range and the phase outputs are a quarter cycle apart") {
  TidesEngine e;
  e.prepare(48000.0);
  e.reset();
  const ParamValues p = defaults();  // 2 Hz: a cycle is 1500 ticks of 16 samples
  const ModContext ctx;
  float out[kMaxModOutputs];
  std::vector<float> out1, out2;
  for (int t = 0; t < 3000; ++t) {
    e.process(ctx, p, 16, out);
    for (int k = 0; k < 4; ++k) {
      REQUIRE(out[k] >= -1.0f);
      REQUIRE(out[k] <= 1.0f);
    }
    out1.push_back(out[0]);
    out2.push_back(out[1]);
  }
  REQUIRE(*std::min_element(out1.begin(), out1.end()) < -0.9f);
  REQUIRE(*std::max_element(out1.begin(), out1.end()) > 0.9f);
  // Shift 100 %: output 2 plays output 1's wave a quarter cycle (375 ticks) later.
  for (size_t t = 500; t < 3000; ++t) REQUIRE(std::fabs(out2[t] - out1[t - 375]) < 0.01f);
}

TEST_CASE("A new note restarts the LFO's cycle") {
  // Two LFOs that have run for different times, retriggered on the same tick, play the same
  // cycle from there (to within the native block the edge lands in: 8 samples at 62.5 kHz).
  auto afterRetrigger = [](int warmUpTicks) {
    TidesEngine e;
    e.prepare(48000.0);
    e.reset();
    const ParamValues p = defaults();
    ModContext ctx;
    ctx.gate = true;
    float out[kMaxModOutputs];
    for (int t = 0; t < warmUpTicks; ++t) e.process(ctx, p, 16, out);
    ctx.retrigger = true;
    e.process(ctx, p, 16, out);
    ctx.retrigger = false;
    std::vector<float> values;
    for (int t = 0; t < 50; ++t) {
      e.process(ctx, p, 16, out);
      values.push_back(out[0]);
    }
    return values;
  };
  const auto a = afterRetrigger(100);
  const auto b = afterRetrigger(777);
  for (size_t t = 0; t < a.size(); ++t) REQUIRE(std::fabs(a[t] - b[t]) < 0.005f);
}

TEST_CASE("A synced LFO locks to the beat") {
  ParamValues p = defaults();
  p.p[1] = modOptionValue(1);  // Sync on; Frequency 50 %: one cycle per beat
  for (double rate : {44100.0, 48000.0, 96000.0}) {
    INFO("rate " << rate);
    const double bps = 2.0 / rate;  // 120 BPM
    const int ticks = int(4.0 * rate / 16.0);  // 4 s: 8 beats
    // b starts about two beats late, mid-cycle, on a tick where the host and native sample grids
    // coincide again (whole periods of rate / gcd(rate, 62500) host samples), so a and b sample
    // the same native samples.
    const auto hostRate = static_cast<long long>(rate);
    const long long period = hostRate / std::gcd(hostRate, 62500LL);
    const int align = int(period / std::gcd(period, 16LL));
    const int lateStart = ticks / 2 / align * align;
    TidesEngine a, b;
    a.prepare(rate);
    b.prepare(rate);
    a.reset();
    float oa[kMaxModOutputs], ob[kMaxModOutputs];
    int rising = 0;
    float previous = 0.0f;
    for (int t = 0; t < ticks; ++t) {
      ModContext ctx;
      ctx.beat = 3.25 + double(t) * 16.0 * bps;
      ctx.beatsPerSample = bps;
      a.process(ctx, p, 16, oa);
      if (previous < 0.0f && oa[0] >= 0.0f) ++rising;
      previous = oa[0];
      if (t == lateStart) b.reset();
      if (t >= lateStart) {
        b.process(ctx, p, 16, ob);
        if (t >= lateStart + 10) REQUIRE(std::fabs(oa[0] - ob[0]) < 1e-5f);
      }
    }
    REQUIRE(rising >= 7);
    REQUIRE(rising <= 9);
  }
}

TEST_CASE("LFO survives NaN and does not allocate") {
  TidesEngine e;
  e.prepare(48000.0);
  e.reset();
  ParamValues bad;
  for (float& v : bad.p) v = std::numeric_limits<float>::quiet_NaN();
  ModContext ctx;
  float out[kMaxModOutputs];
  for (int t = 0; t < 500; ++t) {
    e.process(ctx, bad, 16, out);
    for (int k = 0; k < 4; ++k) REQUIRE(std::isfinite(out[k]));
  }
  ParamValues p = defaults();
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int t = 0; t < 2000; ++t) {
      ctx.gate = (t / 100) % 2 == 0;
      ctx.beat = double(t) * 0.01;
      ctx.beatsPerSample = 0.01 / 16.0;
      p.p[0] = modOptionValue(t / 700);
      p.p[1] = modOptionValue((t / 300) % 2);
      p.p[9] = modOptionValue((t / 250) % 4);
      e.process(ctx, p, 1 + t % 16, out);
    }
    e.reset();
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}
