#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "params/param_ids.h"
#include "synth/param_table.h"
#include "synth/synth.h"
#include "test_helpers.h"

using namespace eg13;

namespace {

constexpr double kRate = 48000.0;

struct Stereo {
  std::vector<float> l, r;
  explicit Stereo(size_t n) : l(n), r(n) {}
};

void run(Synth& synth, Stereo& out, size_t from, size_t to, const Stereo* in = nullptr,
         int chunk = 512) {
  for (size_t done = from; done < to;) {
    const int count = int(std::min<size_t>(size_t(chunk), to - done));
    float* o[2] = {out.l.data() + done, out.r.data() + done};
    if (in) {
      const float* i[2] = {in->l.data() + done, in->r.data() + done};
      synth.process(i, o, count);
    } else {
      synth.process(nullptr, o, count);
    }
    done += size_t(count);
  }
}

Stereo sine(size_t n) {
  Stereo s(n);
  for (size_t i = 0; i < n; ++i) {
    s.l[i] = 0.5f * float(std::sin(2.0 * M_PI * 220.0 * double(i) / kRate));
    s.r[i] = -s.l[i];
  }
  return s;
}

}  // namespace

TEST_CASE("Parameter table lists Macro Oscillator P1-P8 and master level") {
  REQUIRE(kParamCount == 9);
  REQUIRE(paramSpec(0).id == params::id(0, params::Slot::Source, 1));
  REQUIRE(std::string(paramSpec(0).name) == "Model");
  REQUIRE(std::string(paramSpec(0).module) == "Part A/Source");
  REQUIRE(paramSpec(kMasterLevelIndex).id == params::kMasterLevel);
  REQUIRE(paramIndex(params::kMasterLevel) == int(kMasterLevelIndex));
  REQUIRE(paramIndex(0xDEADBEEF) == -1);
  char buf[64];
  formatParam(kMasterLevelIndex, 0.5, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "50%");
}

TEST_CASE("Synth starts at default parameter values") {
  Synth synth(PluginKind::Instrument);
  for (size_t i = 0; i < kParamCount; ++i) {
    REQUIRE(synth.param(paramSpec(i).id) == double(paramSpec(i).defaultValue));
  }
}

TEST_CASE("setParam clamps and rejects NaN and unknown IDs") {
  Synth synth(PluginKind::Instrument);
  const uint32_t id = params::kMasterLevel;
  REQUIRE(synth.setParam(id, 1.5));
  REQUIRE(synth.param(id) == 1.0);
  REQUIRE(synth.setParam(id, -2.0));
  REQUIRE(synth.param(id) == 0.0);
  REQUIRE(synth.setParam(id, 0.25));
  REQUIRE_FALSE(synth.setParam(id, std::numeric_limits<double>::quiet_NaN()));
  REQUIRE(synth.param(id) == 0.25);
  REQUIRE_FALSE(synth.setParam(0xDEADBEEF, 0.5));
}

TEST_CASE("Instrument is silent without notes and sounds with one") {
  Synth synth(PluginKind::Instrument);
  synth.prepare(kRate, 512);
  Stereo out(48000);
  run(synth, out, 0, 24000);
  REQUIRE(test::rms(out.l, 0, 24000) < 1e-4);
  synth.noteOn(60, 1.0f);
  run(synth, out, 24000, 48000);
  REQUIRE(test::rms(out.l, 24000, 48000) > 0.01);
  REQUIRE(out.l == out.r);
}

TEST_CASE("Master level at zero mutes the output") {
  Synth synth(PluginKind::Instrument);
  synth.prepare(kRate, 512);
  synth.setParam(params::kMasterLevel, 0.0);
  synth.noteOn(60, 1.0f);
  Stereo out(4800);
  run(synth, out, 0, 4800);
  for (float s : out.l) REQUIRE(s == 0.0f);
}

TEST_CASE("Mono synth returns to the previous held note") {
  Synth synth(PluginKind::Instrument);
  synth.prepare(kRate, 512);
  Stereo out(96000);
  synth.noteOn(60, 1.0f);
  run(synth, out, 0, 24000);
  synth.noteOn(67, 1.0f);
  run(synth, out, 24000, 48000);
  synth.noteOff(67);
  run(synth, out, 48000, 96000);
  REQUIRE_THAT(test::estimateFrequency(out.l, kRate, 30000, 48000),
               Catch::Matchers::WithinRel(392.0, 0.01));
  REQUIRE_THAT(test::estimateFrequency(out.l, kRate, 60000, 96000),
               Catch::Matchers::WithinRel(261.63, 0.01));
  synth.setParam(params::id(0, params::Slot::Source, 6), 0.0);
  synth.noteOff(60);
  Stereo tail(48000);
  run(synth, tail, 0, 48000);
  REQUIRE(test::rms(tail.l, 43200, 48000) < 1e-3);
}

TEST_CASE("Event timing is independent of block slicing") {
  auto renderWith = [](int chunk) {
    Synth synth(PluginKind::Instrument);
    synth.prepare(kRate, 4096);
    Stereo out(9000);
    run(synth, out, 0, 1001, nullptr, chunk);
    synth.noteOn(64, 0.9f);
    run(synth, out, 1001, 9000, nullptr, chunk);
    return out.l;
  };
  const auto a = renderWith(1);
  REQUIRE(a == renderWith(7));
  REQUIRE(a == renderWith(4096));
}

TEST_CASE("FX passes its input through when no notes play") {
  Synth synth(PluginKind::Effect);
  synth.prepare(kRate, 512);
  const Stereo in = sine(4800);
  Stereo out(4800);
  run(synth, out, 0, 4800, &in);
  for (size_t i = 0; i < 4800; ++i) {
    REQUIRE(std::fabs(out.l[i] - in.l[i]) < 1e-4f);
    REQUIRE(std::fabs(out.r[i] - in.r[i]) < 1e-4f);
  }
}

TEST_CASE("FX passthrough survives in-place buffers") {
  Synth synth(PluginKind::Effect);
  synth.prepare(kRate, 512);
  const Stereo original = sine(4800);
  Stereo buffer = original;
  for (size_t done = 0; done < 4800; done += 480) {
    float* io[2] = {buffer.l.data() + done, buffer.r.data() + done};
    synth.process(io, io, 480);
  }
  for (size_t i = 0; i < 4800; ++i) REQUIRE(std::fabs(buffer.l[i] - original.l[i]) < 1e-4f);
}

TEST_CASE("Instrument ignores audio input") {
  Synth synth(PluginKind::Instrument);
  synth.prepare(kRate, 512);
  const Stereo in = sine(4800);
  Stereo out(4800);
  run(synth, out, 0, 4800, &in);
  REQUIRE(test::rms(out.l, 0, 4800) < 1e-4);
}
