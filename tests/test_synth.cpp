#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "alloc_counter.h"
#include "engine/engine_registry.h"
#include "engine/ripples_engine.h"
#include "engine/tides_engine.h"
#include "engine/warps_engine.h"
#include "params/param_ids.h"
#include "synth/param_table.h"
#include "synth/synth.h"
#include "synth_helpers.h"
#include "test_engines.h"
#include "test_helpers.h"

using namespace eg13;

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

SourceSpec sine() { return {std::make_unique<test::SineEngine>(), 48000.0, 12}; }

uint32_t partId(int part, PartParam p) { return paramSpec(partParamIndex(part, p)).id; }
uint32_t masterId(MasterParam m) { return paramSpec(masterParamIndex(m)).id; }
uint32_t filterTypeId(int part) { return paramSpec(filterEngineParamIndex(part)).id; }
uint32_t shaperTypeId(int part) { return paramSpec(shaperEngineParamIndex(part)).id; }
uint32_t filterId(int part, int p) { return paramSpec(filterParamIndex(part, p)).id; }
uint32_t shaperId(int part, int p) { return paramSpec(shaperParamIndex(part, p)).id; }

uint32_t partFxTypeId(int part) { return paramSpec(partFxEngineParamIndex(part)).id; }
uint32_t partFxId(int part, int p) { return paramSpec(partFxParamIndex(part, p)).id; }

// The exact pass-through tests below predate the Filter and the Part FX: they run both Off.
void filtersOff(Synth& synth) {
  for (int part = 0; part < kNumParts; ++part) {
    synth.setParam(filterTypeId(part), 0);
    synth.setParam(partFxTypeId(part), 0);
  }
}

struct Stereo {
  std::vector<float> l, r;
  explicit Stereo(size_t n) : l(n), r(n) {}
};

void run(Synth& synth, Stereo& out, size_t from, size_t to, const Stereo* in = nullptr,
         Stereo* bus2 = nullptr, int chunk = kBlock) {
  for (size_t done = from; done < to;) {
    const int count = int(std::min<size_t>(size_t(chunk), to - done));
    float* o[2] = {out.l.data() + done, out.r.data() + done};
    float* b[2] = {bus2 ? bus2->l.data() + done : nullptr, bus2 ? bus2->r.data() + done : nullptr};
    const float* i[2] = {in ? in->l.data() + done : nullptr, in ? in->r.data() + done : nullptr};
    synth.process(in ? i : nullptr, o, bus2 ? b : nullptr, count);
    done += size_t(count);
  }
}

Stereo sineInput(size_t n, double rate) {
  Stereo s(n);
  for (size_t i = 0; i < n; ++i) {
    s.l[i] = 0.5f * float(std::sin(6.283185307179586 * 220.0 * double(i) / rate));
    s.r[i] = -s.l[i];
  }
  return s;
}

}  // namespace

TEST_CASE("Synth starts at the defaults for its kind") {
  Synth instrument(PluginKind::Instrument);
  Synth fx(PluginKind::Effect);
  for (size_t i = 0; i < kParamCount; ++i) {
    REQUIRE(instrument.param(paramSpec(i).id) == double(paramDefault(i, PluginKind::Instrument)));
    REQUIRE(fx.param(paramSpec(i).id) == double(paramDefault(i, PluginKind::Effect)));
  }
  REQUIRE(fx.param(masterId(MasterParam::Dry)) == 0.0);
}

TEST_CASE("setParam clamps, rounds and rejects NaN and unknown IDs") {
  Synth synth(PluginKind::Instrument);
  REQUIRE(synth.setParam(params::kMasterLevel, 1.5));
  REQUIRE(synth.param(params::kMasterLevel) == 1.0);
  REQUIRE(synth.setParam(partId(0, PartParam::VoiceCount), 3.4));
  REQUIRE(synth.param(partId(0, PartParam::VoiceCount)) == 3.0);
  REQUIRE(synth.setParam(params::kMasterLevel, 0.25));
  REQUIRE_FALSE(synth.setParam(params::kMasterLevel, std::numeric_limits<double>::quiet_NaN()));
  REQUIRE(synth.param(params::kMasterLevel) == 0.25);
  REQUIRE_FALSE(synth.setParam(0xDEADBEEF, 0.5));
}

TEST_CASE("Instrument is silent without notes and sounds with one") {
  Synth synth(PluginKind::Instrument);
  synth.prepare(kRate, kBlock);
  Stereo out(48000);
  run(synth, out, 0, 24000);
  REQUIRE(test::rms(out.l, 0, 24000) < 1e-4);
  synth.noteOn(0, 60, 1.0f);
  run(synth, out, 24000, 48000);
  REQUIRE(test::rms(out.l, 24000, 48000) > 0.01);
}

TEST_CASE("Master level at zero mutes the synth") {
  Synth synth(PluginKind::Instrument, sine);
  synth.prepare(kRate, kBlock);
  synth.setParam(params::kMasterLevel, 0.0);
  synth.noteOn(0, 60, 1.0f);
  Stereo out(19200);
  run(synth, out, 0, 19200);
  for (size_t i = 14400; i < 19200; ++i) REQUIRE(out.l[i] == 0.0f);
}

TEST_CASE("A chord plays polyphonically on Part A") {
  Synth synth(PluginKind::Instrument, sine);
  synth.prepare(kRate, kBlock);
  for (int key : {60, 64, 67}) synth.noteOn(0, key, 1.0f);
  Stereo out(kBlock);
  run(synth, out, 0, kBlock);
  REQUIRE(synth.activeVoiceCount(0) == 3);
  REQUIRE(synth.activeVoiceCount(1) == 0);
}

TEST_CASE("Modes route notes to parts") {
  Stereo out(kBlock);
  SECTION("Layer sends every note to both parts") {
    Synth synth(PluginKind::Instrument, sine);
    synth.setParam(masterId(MasterParam::Mode), double(PartMode::Layer));
    synth.prepare(kRate, kBlock);
    synth.noteOn(0, 60, 1.0f);
    run(synth, out, 0, kBlock);
    REQUIRE(synth.activeVoiceCount(0) == 1);
    REQUIRE(synth.activeVoiceCount(1) == 1);
  }
  SECTION("Split sends low keys to A and high keys to B") {
    Synth synth(PluginKind::Instrument, sine);
    synth.setParam(masterId(MasterParam::Mode), double(PartMode::Split));
    synth.setParam(masterId(MasterParam::SplitNote), 60);
    synth.prepare(kRate, kBlock);
    synth.noteOn(0, 48, 1.0f);
    synth.noteOn(0, 60, 1.0f);
    synth.noteOn(0, 72, 1.0f);
    run(synth, out, 0, kBlock);
    REQUIRE(synth.activeVoiceCount(0) == 1);
    REQUIRE(synth.activeVoiceCount(1) == 2);
  }
  SECTION("Multi sends each channel to its part") {
    Synth synth(PluginKind::Instrument, sine);
    synth.setParam(masterId(MasterParam::Mode), double(PartMode::Multi));
    synth.setParam(partId(0, PartParam::MidiChannel), 1);
    synth.setParam(partId(1, PartParam::MidiChannel), 2);
    synth.prepare(kRate, kBlock);
    synth.noteOn(0, 60, 1.0f);
    synth.noteOn(1, 64, 1.0f);
    synth.noteOn(1, 67, 1.0f);
    synth.noteOn(5, 70, 1.0f);  // neither part listens to channel 6
    run(synth, out, 0, kBlock);
    REQUIRE(synth.activeVoiceCount(0) == 1);
    REQUIRE(synth.activeVoiceCount(1) == 2);
  }
  SECTION("Single mode ignores Part B") {
    Synth synth(PluginKind::Instrument, sine);
    synth.prepare(kRate, kBlock);
    synth.noteOn(0, 60, 1.0f);
    run(synth, out, 0, kBlock);
    REQUIRE(synth.activeVoiceCount(1) == 0);
  }
}

TEST_CASE("Note-off follows the note-on routing after settings change") {
  Synth synth(PluginKind::Instrument, sine);
  synth.setParam(masterId(MasterParam::Mode), double(PartMode::Split));
  synth.setParam(masterId(MasterParam::SplitNote), 60);
  synth.prepare(kRate, kBlock);
  Stereo out(9600);
  synth.noteOn(0, 55, 1.0f);  // Part A
  run(synth, out, 0, kBlock);
  synth.setParam(masterId(MasterParam::SplitNote), 50);  // 55 would now go to Part B
  run(synth, out, kBlock, 2 * kBlock);
  synth.noteOff(0, 55);
  run(synth, out, 2 * kBlock, 9600);
  REQUIRE(synth.activeVoiceCount(0) == 0);
}

TEST_CASE("Bus 2 carries parts routed to it, and falls back to main when not connected") {
  Synth synth(PluginKind::Instrument, sine);
  test::m3Patch(synth);
  synth.setParam(partId(0, PartParam::Output), 1);
  synth.prepare(kRate, kBlock);
  synth.noteOn(0, 60, 1.0f);
  Stereo main(4800), bus2(4800);
  run(synth, main, 0, 4800, nullptr, &bus2);
  REQUIRE(test::rms(main.l, 0, 4800) == 0.0);
  REQUIRE(test::rms(bus2.l, 480, 4800) > 0.1);

  Stereo fallback(4800);
  run(synth, fallback, 0, 4800);
  REQUIRE(test::rms(fallback.l, 480, 4800) > 0.1);
}

TEST_CASE("FX passes its input through, delayed by the reported latency") {
  for (double rate : {44100.0, 48000.0}) {
    Synth synth(PluginKind::Effect);
    synth.setParam(masterId(MasterParam::Dry), 1.0);
    synth.setParam(partId(0, PartParam::Trigger), 0);
    synth.prepare(rate, kBlock);
    const int latency = synth.latencySamples();
    const Stereo in = sineInput(8820, rate);
    Stereo out(8820);
    run(synth, out, 0, 8820, &in);
    INFO("rate " << rate << " latency " << latency);
    for (size_t i = size_t(latency); i < 8820; ++i) {
      REQUIRE(std::fabs(out.l[i] - in.l[i - size_t(latency)]) < 1e-4f);
      REQUIRE(std::fabs(out.r[i] - in.r[i - size_t(latency)]) < 1e-4f);
    }
  }
}

TEST_CASE("FX passthrough survives in-place buffers") {
  Synth synth(PluginKind::Effect);
  synth.setParam(masterId(MasterParam::Dry), 1.0);
  synth.setParam(partId(0, PartParam::Trigger), 0);
  synth.prepare(kRate, kBlock);
  const size_t latency = size_t(synth.latencySamples());
  const Stereo original = sineInput(4800, kRate);
  Stereo buffer = original;
  for (size_t done = 0; done < 4800; done += 240) {
    float* io[2] = {buffer.l.data() + done, buffer.r.data() + done};
    synth.process(io, io, nullptr, 240);
  }
  for (size_t i = latency; i < 4800; ++i) {
    REQUIRE(std::fabs(buffer.l[i] - original.l[i - latency]) < 1e-4f);
    REQUIRE(std::fabs(buffer.r[i] - original.r[i - latency]) < 1e-4f);
  }
}

TEST_CASE("Instrument ignores input when Dry is 0") {
  Synth synth(PluginKind::Instrument);
  synth.prepare(kRate, kBlock);
  const Stereo in = sineInput(4800, kRate);
  Stereo out(4800);
  run(synth, out, 0, 4800, &in);
  REQUIRE(test::rms(out.l, 0, 4800) < 1e-4);
}

TEST_CASE("loadParams while processing is applied whole at the next block") {
  Synth synth(PluginKind::Instrument, sine);
  synth.prepare(kRate, kBlock);
  synth.setActive(true);
  ParamSnapshot values = synth.defaultSnapshot();
  values[masterParamIndex(MasterParam::Level)] = 0.5f;
  values[partParamIndex(0, PartParam::VoiceCount)] = 2.0f;
  synth.loadParams(values);
  REQUIRE(synth.param(params::kMasterLevel) == 0.5);  // visible before the audio thread runs
  REQUIRE(synth.param(partId(0, PartParam::VoiceCount)) == 2.0);
  Stereo out(kBlock);
  run(synth, out, 0, kBlock);
  for (int key : {60, 64, 67}) synth.noteOn(0, key, 1.0f);
  run(synth, out, 0, kBlock);
  REQUIRE(synth.activeVoiceCount(0) == 2);
  REQUIRE(synth.param(params::kMasterLevel) == 0.5);
  synth.setActive(false);
}

TEST_CASE("loadParams when not processing applies immediately") {
  Synth synth(PluginKind::Instrument);
  ParamSnapshot values = synth.defaultSnapshot();
  values[masterParamIndex(MasterParam::Level)] = 0.3f;
  values[partParamIndex(1, PartParam::VoiceCount)] = 99.0f;  // clamped
  synth.loadParams(values);
  REQUIRE(synth.param(params::kMasterLevel) == double(0.3f));
  REQUIRE(synth.param(partId(1, PartParam::VoiceCount)) == 8.0);
  REQUIRE(synth.snapshot()[masterParamIndex(MasterParam::Level)] == 0.3f);
}

TEST_CASE("Mono voice mode returns to the previous held note") {
  Synth synth(PluginKind::Instrument);
  synth.setParam(partId(0, PartParam::VoiceMode), double(VoiceMode::Mono));
  synth.prepare(kRate, kBlock);
  Stereo out(96000);
  synth.noteOn(0, 60, 1.0f);
  run(synth, out, 0, 24000);
  synth.noteOn(0, 67, 1.0f);
  run(synth, out, 24000, 48000);
  synth.noteOff(0, 67);
  run(synth, out, 48000, 96000);
  REQUIRE_THAT(test::estimateFrequency(out.l, kRate, 30000, 48000),
               Catch::Matchers::WithinRel(392.0, 0.01));
  REQUIRE_THAT(test::estimateFrequency(out.l, kRate, 60000, 96000),
               Catch::Matchers::WithinRel(261.63, 0.01));
  REQUIRE(synth.activeVoiceCount(0) == 1);
}

TEST_CASE("Pitch bend and the sustain pedal reach the part") {
  Synth synth(PluginKind::Instrument, sine);
  synth.prepare(kRate, kBlock);
  Stereo out(48000);
  synth.noteOn(0, 57, 1.0f);
  synth.pitchBend(0, 1.0f);  // +2 semitones
  run(synth, out, 0, 48000);
  REQUIRE_THAT(test::estimateFrequency(out.l, kRate, 4800, 48000),
               Catch::Matchers::WithinRel(220.0 * std::pow(2.0, 2.0 / 12.0), 0.001));

  synth.controlChange(0, 64, 127);
  synth.noteOff(0, 57);
  run(synth, out, 0, 9600);
  REQUIRE(synth.activeVoiceCount(0) == 1);
  synth.controlChange(0, 64, 0);
  run(synth, out, 0, 9600);
  REQUIRE(synth.activeVoiceCount(0) == 0);
}

TEST_CASE("All notes off (CC 123) silences held notes") {
  Synth synth(PluginKind::Instrument, sine);
  synth.prepare(kRate, kBlock);
  Stereo out(9600);
  for (int key : {60, 64}) synth.noteOn(0, key, 1.0f);
  run(synth, out, 0, kBlock);
  synth.controlChange(0, 123, 0);
  run(synth, out, 0, 9600);
  REQUIRE(synth.activeVoiceCount(0) == 0);
}

TEST_CASE("An Always-on part sounds with no MIDI") {
  Synth synth(PluginKind::Effect, sine);
  synth.setParam(partFxTypeId(0), 0);
  synth.setParam(partId(0, PartParam::Trigger), 1);
  synth.setParam(masterId(MasterParam::Dry), 0.0);
  synth.prepare(kRate, kBlock);
  Stereo out(9600);
  run(synth, out, 0, 9600);
  REQUIRE(synth.activeVoiceCount(0) == 1);
  REQUIRE(test::rms(out.l, 4800, 9600) > 0.1);
}

TEST_CASE("Event timing is independent of block slicing") {
  // The instrument's init patch: Mod 1's envelope on the Amp, Mod 2's on the State Variable
  // Filter's cutoff, Mod 3's LFO. A host-rate Filter ramps a modulated control across each call,
  // so with host blocks shorter than 16 samples the ramp is shorter (see the manual's Smoothing
  // page): renders then differ by an inaudible amount. With the Filter Off they are identical.
  auto renderWith = [](int chunk, bool filter) {
    Synth synth(PluginKind::Instrument);
    if (!filter) filtersOff(synth);
    synth.prepare(44100.0, 4096);
    Stereo out(9000);
    run(synth, out, 0, 1001, nullptr, nullptr, chunk);
    synth.noteOn(0, 64, 0.9f);
    run(synth, out, 1001, 9000, nullptr, nullptr, chunk);
    return out.l;
  };
  const auto a = renderWith(1, false);
  REQUIRE(a == renderWith(7, false));
  REQUIRE(a == renderWith(4096, false));
  const auto f = renderWith(4096, true);
  REQUIRE(test::rms(f, 1001, 9000) > 0.01);
  // Measured (44.1 kHz, peak 0.112): at most 1.41e-3 at block 1 and 3.98e-4 at block 7, both on
  // the note's attack, where Mod 2 opens the cutoff fastest (a 1 ms attack); about 1 % of the peak.
  // The bound leaves a margin of about 1.4 over block 1, and a regression that dropped the per-tick
  // delivery would differ by far more.
  for (int chunk : {1, 7}) {
    const auto g = renderWith(chunk, true);
    float worst = 0.0f;
    for (size_t i = 0; i < f.size(); ++i) worst = std::max(worst, std::fabs(f[i] - g[i]));
    CAPTURE(chunk, worst);
    REQUIRE(worst < 2e-3f);
  }
}

TEST_CASE("Synth processing does not allocate") {
  Synth synth(PluginKind::Effect);
  synth.setParam(masterId(MasterParam::Mode), double(PartMode::Layer));
  synth.prepare(44100.0, kBlock);
  synth.setActive(true);
  Stereo in = sineInput(kBlock, 44100.0), out(kBlock), bus2(kBlock);
  run(synth, out, 0, kBlock, &in, &bus2);
  test::ScopedAllocationCounter counter;
  for (int block = 0; block < 200; ++block) {
    if (block == 10) for (int key : {60, 64, 67}) synth.noteOn(0, key, 0.8f);
    if (block == 40) synth.pitchBend(0, -0.5f);
    if (block == 60) synth.setParam(partId(1, PartParam::VoiceCount), 2);
    if (block == 80) synth.controlChange(0, 64, 127);
    if (block == 90) synth.noteOff(0, 64);
    if (block == 120) synth.controlChange(0, 64, 0);
    if (block == 150) synth.setParam(masterId(MasterParam::Mode), double(PartMode::Split));
    if (block == 170) synth.reset();
    run(synth, out, 0, kBlock, &in, &bus2);
  }
  const size_t allocations = counter.count();
  REQUIRE(allocations == 0);
  synth.setActive(false);
}

TEST_CASE("A pending snapshot is applied before same-block automation") {
  Synth synth(PluginKind::Instrument, sine);
  synth.prepare(kRate, kBlock);
  synth.setActive(true);
  ParamSnapshot values = synth.defaultSnapshot();
  values[masterParamIndex(MasterParam::Level)] = 0.5f;
  synth.loadParams(values);
  synth.beginBlock();
  synth.setParam(params::kMasterLevel, 0.2);
  Stereo out(kBlock);
  run(synth, out, 0, kBlock);
  REQUIRE(synth.param(params::kMasterLevel) == double(0.2f));
  synth.setActive(false);
}

TEST_CASE("Deactivating applies a pending snapshot") {
  Synth synth(PluginKind::Instrument, sine);
  synth.prepare(kRate, kBlock);
  synth.setActive(true);
  ParamSnapshot values = synth.defaultSnapshot();
  values[masterParamIndex(MasterParam::Level)] = 0.4f;
  synth.loadParams(values);
  synth.setActive(false);
  REQUIRE(synth.param(params::kMasterLevel) == double(0.4f));
  values[masterParamIndex(MasterParam::Level)] = 0.6f;
  synth.loadParams(values);
  REQUIRE(synth.param(params::kMasterLevel) == double(0.6f));
}

TEST_CASE("Two loads in a row while active never block") {
  Synth synth(PluginKind::Instrument, sine);
  synth.prepare(kRate, kBlock);
  synth.setActive(true);
  ParamSnapshot values = synth.defaultSnapshot();
  values[masterParamIndex(MasterParam::Level)] = 0.3f;
  synth.loadParams(values);
  values[masterParamIndex(MasterParam::Level)] = 0.7f;
  synth.loadParams(values);
  REQUIRE(synth.param(params::kMasterLevel) == double(0.7f));
  synth.setActive(false);
}

TEST_CASE("Master level does not scale Dry") {
  Synth synth(PluginKind::Effect);
  synth.setParam(masterId(MasterParam::Dry), 1.0);
  synth.setParam(partId(0, PartParam::Trigger), 0);
  synth.setParam(params::kMasterLevel, 0.0);
  synth.prepare(kRate, kBlock);
  const size_t latency = size_t(synth.latencySamples());
  const Stereo in = sineInput(4800, kRate);
  Stereo out(4800);
  run(synth, out, 0, 4800, &in);
  for (size_t i = latency; i < 4800; ++i) {
    REQUIRE(std::fabs(out.l[i] - in.l[i - latency]) < 1e-4f);
  }
}

TEST_CASE("Dry drops stale input after a gap") {
  Synth synth(PluginKind::Effect);
  synth.setParam(masterId(MasterParam::Dry), 1.0);
  synth.setParam(partId(0, PartParam::Trigger), 0);
  synth.prepare(44100.0, kBlock);
  const Stereo in = sineInput(4410, 44100.0);
  const Stereo silent(13230);
  Stereo out(3 * 4410);
  run(synth, out, 0, 4410, &in);
  run(synth, out, 4410, 8820);
  run(synth, out, 8820, 13230, &silent);
  for (size_t i = 8820; i < 13230; ++i) REQUIRE(std::fabs(out.l[i]) < 1e-6f);
}

TEST_CASE("Single mode mutes Part B audio") {
  Synth synth(PluginKind::Effect, sine);
  synth.setParam(partId(0, PartParam::Trigger), 0);
  synth.setParam(masterId(MasterParam::Dry), 0.0);
  synth.setParam(partId(1, PartParam::Trigger), 1);
  synth.prepare(kRate, kBlock);
  Stereo out(38400);
  run(synth, out, 0, 19200);
  REQUIRE(test::rms(out.l, 14400, 19200) == 0.0);
  synth.setParam(masterId(MasterParam::Mode), double(PartMode::Layer));
  run(synth, out, 19200, 38400);
  REQUIRE(test::rms(out.l, 33600, 38400) > 0.1);
}

TEST_CASE("Mono in-place input is delayed once") {
  Synth synth(PluginKind::Effect);
  synth.setParam(masterId(MasterParam::Dry), 1.0);
  synth.setParam(partId(0, PartParam::Trigger), 0);
  synth.prepare(kRate, kBlock);
  const size_t latency = size_t(synth.latencySamples());
  const Stereo original = sineInput(4800, kRate);
  std::vector<float> buffer = original.l;
  for (size_t done = 0; done < 4800; done += 240) {
    float* io[2] = {buffer.data() + done, buffer.data() + done};
    synth.process(io, io, nullptr, 240);
  }
  for (size_t i = latency; i < 4800; ++i) {
    REQUIRE(std::fabs(buffer[i] - original.l[i - latency]) < 1e-4f);
  }
}

namespace {

uint32_t sourceId(int part, int p) { return paramSpec(sourceParamIndex(part, p)).id; }
uint32_t engineId(int part) { return paramSpec(sourceEngineParamIndex(part)).id; }

void runBlocks(Synth& synth, int blocks, int n = kBlock) {
  std::vector<float> l(static_cast<size_t>(n)), r(static_cast<size_t>(n));
  float* out[2] = {l.data(), r.data()};
  for (int b = 0; b < blocks; ++b) synth.process(nullptr, out, nullptr, n);
}

}  // namespace

TEST_CASE("Switching engines swaps P values and reports them") {
  Synth synth(PluginKind::Instrument);
  synth.prepare(kRate, kBlock);
  synth.setActive(true);
  synth.setParam(sourceId(0, 1), 0.9);  // Macro Harmonics
  synth.setParam(engineId(0), 1);       // Digital Oscillator
  runBlocks(synth, 1);
  REQUIRE(synth.engine(0) == 1);
  REQUIRE(synth.param(sourceId(0, 0)) == double(sourceEngineInfo(1).params[0].defaultValue));
  HostUpdate updates[2 * kSourceParamCount];
  REQUIRE(synth.takeHostUpdates(updates, int(2 * kSourceParamCount)) == int(kSourceParamCount));
  REQUIRE(updates[0].id == sourceId(0, 0));
  REQUIRE(updates[15].id == sourceId(0, 15));
  REQUIRE(synth.takeEngineChanged());
  REQUIRE_FALSE(synth.takeEngineChanged());

  synth.setParam(sourceId(0, 1), 0.3);  // Digital Timbre
  synth.setParam(engineId(0), 0);
  runBlocks(synth, 1);
  REQUIRE(synth.param(sourceId(0, 1)) == double(0.9f));
  synth.setParam(engineId(0), 1);
  runBlocks(synth, 1);
  REQUIRE(synth.param(sourceId(0, 1)) == double(0.3f));
  synth.setActive(false);
}

TEST_CASE("Engine-aware names and text follow the selected engine") {
  Synth synth(PluginKind::Instrument);
  synth.setParam(engineId(1), 3);  // inactive: applies at once
  REQUIRE(std::string(synth.paramName(sourceParamIndex(1, 0))) == "Model");
  REQUIRE(std::string(synth.paramName(sourceParamIndex(1, 5))) == "Exciter");
  char buf[64];
  synth.formatParam(sourceParamIndex(1, 0), 0.5 / 6.0, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Modal");
}

TEST_CASE("An unknown engine selector value plays the Macro Oscillator") {
  Synth synth(PluginKind::Instrument);
  synth.setParam(engineId(0), 17);
  synth.prepare(kRate, kBlock);
  runBlocks(synth, 1);
  REQUIRE(synth.engine(0) == 0);
}

TEST_CASE("EG13 FX with the Filter Off passes audio at unity through Part A") {
  Synth synth(PluginKind::Effect);
  filtersOff(synth);
  synth.prepare(44100.0, kBlock);
  REQUIRE(synth.engine(0) == int(SourceEngine::AudioInput));
  const size_t latency = size_t(synth.latencySamples());
  const Stereo in = sineInput(13230, 44100.0);
  Stereo out(13230);
  run(synth, out, 0, 13230, &in);
  for (size_t i = latency + 2205; i < 13230; ++i) {
    REQUIRE(std::fabs(out.l[i] - in.l[i - latency]) < 1e-4f);
  }
}

TEST_CASE("FX: processed input is aligned with Dry") {
  Synth synth(PluginKind::Effect);
  test::m3Patch(synth);  // wet + Dry reach full scale: no soft clipping here
  filtersOff(synth);
  synth.setParam(masterId(MasterParam::Dry), 1.0);
  synth.prepare(44100.0, kBlock);
  const size_t latency = size_t(synth.latencySamples());
  Stereo in(13230);
  in.l[8000] = in.r[8000] = 0.5f;
  Stereo out(13230);
  run(synth, out, 0, 13230, &in);
  REQUIRE(std::fabs(out.l[8000 + latency] - 1.0f) < 1e-3f);  // wet + dry on the same sample
  REQUIRE(std::fabs(out.l[8000 + latency - 1]) < 1e-3f);
  REQUIRE(std::fabs(out.l[8000 + latency + 1]) < 1e-3f);
}

TEST_CASE("Part B can take Part A as its input") {
  Synth synth(PluginKind::Effect);
  filtersOff(synth);
  synth.setParam(masterId(MasterParam::Mode), double(PartMode::Layer));
  synth.setParam(partId(0, PartParam::Output), 2);  // Part A: other part only
  synth.setParam(engineId(1), 4);                    // Part B: Audio Input
  synth.setParam(partId(1, PartParam::Trigger), 1);
  synth.setParam(partId(1, PartParam::InputSource), 1);
  synth.prepare(44100.0, kBlock);
  const size_t latency = size_t(synth.latencySamples());
  const Stereo in = sineInput(13230, 44100.0);
  Stereo out(13230);
  run(synth, out, 0, 13230, &in);
  for (size_t i = 2 * latency + 4410; i < 13230; ++i) {
    REQUIRE(std::fabs(out.l[i] - in.l[i - 2 * latency]) < 1e-3f);
  }
}

TEST_CASE("A part with Output = Other part only is not heard") {
  Synth synth(PluginKind::Instrument, sine);
  synth.setParam(partId(0, PartParam::Output), 2);
  synth.prepare(kRate, kBlock);
  synth.noteOn(0, 60, 1.0f);
  Stereo out(4800);
  run(synth, out, 0, 4800);
  for (float s : out.l) REQUIRE(s == 0.0f);
}

TEST_CASE("A feedback loop between parts stays bounded") {
  Synth synth(PluginKind::Effect);
  synth.setParam(masterId(MasterParam::Mode), double(PartMode::Layer));
  synth.setParam(sourceId(0, 0), 1.0);  // Part A gain +12 dB
  synth.setParam(partId(0, PartParam::InputSource), 2);  // external + other part
  synth.setParam(engineId(1), 4);
  synth.setParam(sourceId(1, 0), 1.0);
  synth.setParam(partId(1, PartParam::Trigger), 1);
  synth.setParam(partId(1, PartParam::InputSource), 1);
  synth.prepare(44100.0, kBlock);
  const Stereo in = sineInput(88200, 44100.0);
  Stereo out(88200);
  run(synth, out, 0, 88200, &in);
  for (float s : out.l) {
    REQUIRE(std::isfinite(s));
    REQUIRE(std::fabs(s) < 32.0f);  // each feed is limited to 2; gains of 4 keep it far below
  }
}

TEST_CASE("loadParams adopts the snapshot's engine and stash") {
  Synth synth(PluginKind::Instrument);
  ParamSnapshot values = synth.defaultSnapshot();
  values[sourceEngineParamIndex(0)] = 2.0f;  // Modal Synth
  values[sourceParamIndex(0, 3)] = 0.4f;     // its Strike
  EngineStash stash = Synth::defaultStash();
  stash[stashIndex(0, 0, 1)] = 0.77f;        // Macro Harmonics, kept for later
  synth.loadParams(values, &stash);
  REQUIRE(synth.engine(0) == 2);
  REQUIRE(synth.param(sourceId(0, 3)) == double(0.4f));
  const EngineStash saved = synth.engineStash();
  REQUIRE(saved[stashIndex(0, 0, 1)] == 0.77f);
  REQUIRE(saved[stashIndex(0, 2, 3)] == 0.4f);  // current engine: live value
  synth.prepare(kRate, kBlock);
  synth.setParam(engineId(0), 0);
  runBlocks(synth, 1);
  REQUIRE(synth.param(sourceId(0, 1)) == double(0.77f));
}

TEST_CASE("Engine switching and input routing do not allocate") {
  Synth synth(PluginKind::Effect);
  synth.setParam(masterId(MasterParam::Mode), double(PartMode::Layer));
  synth.setParam(masterId(MasterParam::Dry), 0.5);
  synth.prepare(44100.0, kBlock);
  synth.setActive(true);
  Stereo in = sineInput(kBlock, 44100.0), out(kBlock), bus2(kBlock);
  run(synth, out, 0, kBlock, &in, &bus2);
  HostUpdate updates[16];
  test::ScopedAllocationCounter counter;
  for (int block = 0; block < 200; ++block) {
    if (block == 10) synth.setParam(engineId(1), 4);
    if (block == 20) synth.setParam(partId(1, PartParam::Trigger), 1);
    if (block == 30) synth.setParam(partId(1, PartParam::InputSource), 1);
    if (block == 40) synth.setParam(partId(0, PartParam::InputSource), 2);
    if (block == 50) synth.setParam(partId(0, PartParam::Output), 2);
    if (block == 60) synth.setParam(partId(1, PartParam::Output), 1);
    if (block == 70) synth.setParam(engineId(0), 1);
    if (block == 80) synth.noteOn(0, 60, 0.8f);
    if (block == 90) synth.setParam(engineId(0), 2);
    if (block == 100) synth.setParam(engineId(0), 3);
    if (block == 110) synth.setParam(engineId(0), 0);
    if (block == 120) synth.setParam(engineId(0), 4);
    if (block == 130) synth.setParam(engineId(1), 2);
    if (block == 140) synth.setParam(partId(1, PartParam::InputSource), 2);
    if (block == 150) synth.reset();
    if (block == 160) synth.setParam(engineId(1), 4);
    run(synth, out, 0, kBlock, &in, &bus2);
    synth.takeHostUpdates(updates, 16);
    synth.takeEngineChanged();
  }
  const size_t allocations = counter.count();
  REQUIRE(allocations == 0);
  synth.setActive(false);
}

TEST_CASE("P values sent after an engine change in the same block stay") {
  Synth synth(PluginKind::Instrument);
  synth.prepare(kRate, kBlock);
  synth.setActive(true);
  synth.beginBlock();
  synth.setParam(engineId(0), 3);       // Resonator
  synth.setParam(sourceId(0, 2), 0.66); // then its P3, same block
  runBlocks(synth, 1);
  REQUIRE(synth.engine(0) == 3);
  REQUIRE(synth.param(sourceId(0, 2)) == double(0.66f));
  synth.setParam(engineId(0), 1);
  synth.setParam(engineId(0), 3);  // two switches before the host takes updates
  HostUpdate updates[2 * kSourceParamCount];
  // One entry per P, latest value.
  REQUIRE(synth.takeHostUpdates(updates, int(2 * kSourceParamCount)) == int(kSourceParamCount));
  REQUIRE(updates[2].value == 0.66f);
  synth.setActive(false);
}

namespace {

const int kVaryingSizes[] = {5, 507, 64, 256};

// Feeds `in` through the synth in the cycling block sizes above (all <= 512).
void runVarying(Synth& synth, Stereo& out, const Stereo& in, size_t total) {
  size_t done = 0;
  for (int k = 0; done < total; ++k) {
    const int count = int(std::min<size_t>(size_t(kVaryingSizes[k % 4]), total - done));
    const float* i[2] = {in.l.data() + done, in.r.data() + done};
    float* o[2] = {out.l.data() + done, out.r.data() + done};
    synth.process(i, o, nullptr, count);
    done += size_t(count);
  }
}

// B: Audio Input on the external input, other part only. A: Audio Input on
// Other part. Each Audio Input part delays by the latency L, so main out is
// the input delayed by 2L + the B -> A feed's fixed delay.
void checkFeedDelay(int maxBlock, size_t feedDelay) {
  Synth synth(PluginKind::Effect);
  filtersOff(synth);
  synth.setParam(masterId(MasterParam::Mode), double(PartMode::Layer));
  synth.setParam(partId(0, PartParam::InputSource), 1);
  synth.setParam(engineId(1), 4);
  synth.setParam(partId(1, PartParam::Trigger), 1);
  synth.setParam(partId(1, PartParam::Output), 2);
  synth.prepare(44100.0, maxBlock);
  const size_t delay = 2 * size_t(synth.latencySamples()) + feedDelay;
  const size_t total = 22050;
  const Stereo in = sineInput(total, 44100.0);
  Stereo out(total);
  if (maxBlock >= 512) {
    runVarying(synth, out, in, total);
  } else {
    run(synth, out, 0, total, &in, nullptr, maxBlock);
  }
  INFO("maxBlock " << maxBlock << " latency " << synth.latencySamples() << " total delay " << delay);
  for (size_t i = delay + 4410; i < total; ++i) {
    REQUIRE(std::fabs(out.l[i] - in.l[i - delay]) < 1e-3f);
    REQUIRE(std::fabs(out.r[i] - in.r[i - delay]) < 1e-3f);
  }
}

}  // namespace

TEST_CASE("Part A hears Part B kFeedBlock samples later, whatever the host block sizes") {
  REQUIRE(Synth::kFeedBlock == 64);
  checkFeedDelay(512, 64);   // varying blocks 5, 507, 64, 256
  checkFeedDelay(4096, 64);  // a large host maxBlock does not lengthen the delay
}

TEST_CASE("The B -> A feed delay is the host block when it is shorter than kFeedBlock") {
  checkFeedDelay(32, 32);
}

TEST_CASE("The B -> A feed does not allocate with varying block sizes") {
  constexpr int kMaxBlock = 512;
  Synth synth(PluginKind::Effect);
  synth.setParam(masterId(MasterParam::Mode), double(PartMode::Layer));
  synth.setParam(partId(0, PartParam::InputSource), 2);
  synth.setParam(engineId(1), 4);
  synth.setParam(partId(1, PartParam::Trigger), 1);
  synth.setParam(partId(1, PartParam::InputSource), 1);
  synth.prepare(44100.0, kMaxBlock);
  synth.setActive(true);
  const Stereo in = sineInput(44100, 44100.0);
  Stereo out(44100);
  runVarying(synth, out, in, 4410);
  test::ScopedAllocationCounter counter;
  runVarying(synth, out, in, 22050);
  synth.reset();
  runVarying(synth, out, in, 22050);
  const size_t allocations = counter.count();
  REQUIRE(allocations == 0);
  synth.setActive(false);
}

TEST_CASE("reset clears the B -> A feed") {
  constexpr int kMaxBlock = 512;
  Synth synth(PluginKind::Effect);
  synth.setParam(masterId(MasterParam::Mode), double(PartMode::Layer));
  synth.setParam(partId(0, PartParam::InputSource), 1);
  synth.setParam(engineId(1), 4);
  synth.setParam(partId(1, PartParam::Trigger), 1);
  synth.setParam(partId(1, PartParam::Output), 2);
  filtersOff(synth);  // the Analog Filter adds VCV's 1e-6 bootstrap noise: exact zeros need it Off
  synth.prepare(44100.0, kMaxBlock);
  const Stereo in = sineInput(8820, 44100.0);
  Stereo out(8820);
  run(synth, out, 0, 8820, &in);
  synth.reset();
  const Stereo silent(4410);
  Stereo after(4410);
  run(synth, after, 0, 4410, &silent);
  for (float s : after.l) REQUIRE(s == 0.0f);
}

TEST_CASE("takeHostUpdates keeps what does not fit") {
  Synth synth(PluginKind::Instrument);
  synth.prepare(kRate, kBlock);
  synth.setActive(true);
  synth.setParam(engineId(0), 1);
  HostUpdate updates[kSourceParamCount];
  constexpr int kAll = int(kSourceParamCount);
  REQUIRE(synth.takeHostUpdates(updates, 3) == 3);
  REQUIRE(updates[0].id == sourceId(0, 0));
  REQUIRE(synth.takeHostUpdates(updates, kAll) == kAll - 3);
  REQUIRE(updates[0].id == sourceId(0, 3));
  REQUIRE(updates[kAll - 4].id == sourceId(0, kAll - 1));
  REQUIRE(synth.takeHostUpdates(updates, kAll) == 0);
  synth.setActive(false);
}

TEST_CASE("loadParams drops host updates from before the load") {
  Synth synth(PluginKind::Instrument);
  synth.prepare(kRate, kBlock);
  synth.setActive(true);
  synth.setParam(engineId(0), 2);
  synth.loadParams(synth.defaultSnapshot());
  runBlocks(synth, 1);
  HostUpdate updates[16];
  REQUIRE(synth.takeHostUpdates(updates, 16) == 0);
  synth.setActive(false);
  synth.setParam(engineId(0), 3);  // inactive load drops them too
  synth.loadParams(synth.defaultSnapshot());
  REQUIRE(synth.takeHostUpdates(updates, 16) == 0);
}

TEST_CASE("An engine switch never shows the new selector with the old engine's values") {
  Synth synth(PluginKind::Instrument);
  synth.setParam(sourceId(0, 1), 0.9);  // Macro Harmonics
  synth.setParam(engineId(0), 1);
  const ParamSnapshot s = synth.snapshot();
  REQUIRE(s[sourceEngineParamIndex(0)] == 1.0f);
  REQUIRE(s[sourceParamIndex(0, 1)] == sourceEngineInfo(1).params[1].defaultValue);
  REQUIRE(synth.engineStash()[stashIndex(0, 0, 1)] == 0.9f);
}

TEST_CASE("The engine stash round-trips all 16 Source slots") {
  Synth synth(PluginKind::Instrument);
  for (int p = 0; p < int(kSourceParamCount); ++p) synth.setParam(sourceId(1, p), 0.01 * (p + 1));
  synth.setParam(engineId(1), 2);  // Modal Synth
  const EngineInfo& modal = sourceEngineInfo(2);
  for (int p = 0; p < int(kSourceParamCount); ++p) {
    INFO(p);
    const float expected = p < modal.paramCount ? modal.params[p].defaultValue : 0.0f;
    REQUIRE(synth.param(sourceId(1, p)) == double(expected));
    REQUIRE(synth.engineStash()[stashIndex(1, 0, p)] == float(0.01 * (p + 1)));
  }
  synth.setParam(sourceId(1, 15), 0.5);  // Modal Synth's slot 16
  synth.setParam(engineId(1), 0);
  for (int p = 0; p < int(kSourceParamCount); ++p) {
    INFO(p);
    REQUIRE(synth.param(sourceId(1, p)) == double(float(0.01 * (p + 1))));
  }
  synth.setParam(engineId(1), 2);
  REQUIRE(synth.param(sourceId(1, 15)) == 0.5);
}

TEST_CASE("Two parts switching fill the host-update queue without dropping any") {
  Synth synth(PluginKind::Instrument);
  synth.setParam(engineId(0), 1);
  synth.setParam(engineId(1), 3);
  synth.setParam(engineId(0), 2);  // coalesces with part A's earlier entries
  HostUpdate updates[2 * kSourceParamCount + 1];
  REQUIRE(synth.takeHostUpdates(updates, int(2 * kSourceParamCount + 1)) == int(2 * kSourceParamCount));
  REQUIRE(updates[0].value == sourceEngineInfo(2).params[0].defaultValue);
}

TEST_CASE("process before prepare outputs silence") {
  Synth synth(PluginKind::Instrument);
  std::vector<float> l(64, 1.0f), r(64, 1.0f), bl(64, 1.0f), br(64, 1.0f);
  float* out[2] = {l.data(), r.data()};
  float* bus2[2] = {bl.data(), br.data()};
  synth.process(nullptr, out, bus2, 64);
  const auto silent = [](const std::vector<float>& x) {
    return std::all_of(x.begin(), x.end(), [](float s) { return s == 0.0f; });
  };
  REQUIRE(silent(l));
  REQUIRE(silent(r));
  REQUIRE(silent(bl));
  REQUIRE(silent(br));
}

TEST_CASE("EG13 FX init patch runs Audio Input through the open Analog Filter") {
  Synth synth(PluginKind::Effect);
  synth.setParam(partFxTypeId(0), 0);  // the Filter alone; the full init patch is tested below
  REQUIRE(synth.engine(SlotKind::Filter, 0) == int(FilterEngine::Analog));
  REQUIRE(synth.engine(SlotKind::Filter, 1) == int(FilterEngine::Off));
  REQUIRE(synth.engine(SlotKind::Shaper, 0) == int(ShaperEngine::Off));
  // The host sees the Analog Filter's own controls at the slot's host defaults, which give the
  // Analog Filter's own default settings: fully open, no resonance, LP4 (docs/manual/engines.md).
  const EngineInfo& analog = filterEngineInfo(int(FilterEngine::Analog));
  REQUIRE(std::string(synth.paramName(filterParamIndex(0, 0))) == "Frequency");
  REQUIRE(synth.param(filterId(0, 0)) == double(analog.params[0].defaultValue));
  REQUIRE(synth.param(filterId(0, 1)) == double(analog.params[1].defaultValue));
  REQUIRE(RipplesEngine::modeIndex(float(synth.param(filterId(0, 2)))) ==
          RipplesEngine::modeIndex(analog.params[2].defaultValue));
  synth.prepare(48000.0, kBlock);
  const size_t latency = size_t(synth.latencySamples());
  const Stereo in = sineInput(48000, 48000.0);
  Stereo out(48000);
  run(synth, out, 0, 48000, &in);
  double dot = 0.0;
  for (size_t i = 9600; i < 48000; ++i) dot += double(out.l[i]) * double(in.l[i - latency]);
  REQUIRE(dot > 0.0);
  const double gainDb = 20.0 * std::log10(test::rms(out.l, 9600, 48000) / test::rms(in.l, 9600, 48000));
  INFO("open Analog Filter gain " << gainDb << " dB");
  REQUIRE(std::fabs(gainDb) < 1.0);
}

TEST_CASE("Switching Filter and Shaper types swaps their P values and reports them") {
  Synth synth(PluginKind::Instrument);
  synth.prepare(kRate, kBlock);
  synth.setActive(true);
  synth.setParam(filterTypeId(0), double(FilterEngine::Analog));
  runBlocks(synth, 1);
  HostUpdate updates[kMaxHostUpdates];
  REQUIRE(synth.takeHostUpdates(updates, int(kMaxHostUpdates)) == int(kFilterParamCount));
  REQUIRE(updates[0].id == filterId(0, 0));
  REQUIRE(updates[0].value == filterEngineInfo(int(FilterEngine::Analog)).params[0].defaultValue);
  REQUIRE(synth.takeEngineChanged());
  synth.setParam(filterId(0, 0), 0.3);
  synth.setParam(filterTypeId(0), double(FilterEngine::Classic));
  runBlocks(synth, 1);
  REQUIRE(synth.param(filterId(0, 0)) == double(filterEngineInfo(int(FilterEngine::Classic)).params[0].defaultValue));
  synth.setParam(filterTypeId(0), double(FilterEngine::Analog));
  runBlocks(synth, 1);
  REQUIRE(synth.param(filterId(0, 0)) == double(0.3f));

  synth.takeHostUpdates(updates, int(kMaxHostUpdates));
  synth.setParam(shaperTypeId(1), double(ShaperEngine::Eq));
  REQUIRE(synth.takeHostUpdates(updates, int(kMaxHostUpdates)) == int(kShaperParamCount));
  REQUIRE(updates[11].id == shaperId(1, 11));
  synth.setParam(shaperId(1, 10), 0.9);  // EQ Input Pad on
  synth.setParam(shaperTypeId(1), double(ShaperEngine::RingMod));
  synth.setParam(shaperTypeId(1), double(ShaperEngine::Eq));
  REQUIRE(synth.param(shaperId(1, 10)) == double(0.9f));
  synth.setActive(false);
}

TEST_CASE("Filter and Shaper names and text follow their engines") {
  Synth synth(PluginKind::Instrument);
  REQUIRE(std::string(synth.paramName(filterParamIndex(0, 0))) == "Cutoff");  // the init patch's SVF
  synth.setParam(filterTypeId(0), double(FilterEngine::Analog));
  REQUIRE(std::string(synth.paramName(filterParamIndex(0, 0))) == "Frequency");
  synth.setParam(filterTypeId(0), double(FilterEngine::StateVariable));
  synth.setParam(shaperTypeId(0), double(ShaperEngine::Eq));
  REQUIRE(std::string(synth.paramName(filterParamIndex(0, 0))) == "Cutoff");
  REQUIRE(std::string(synth.paramName(filterParamIndex(0, 2))) == "Mode");
  REQUIRE(std::string(synth.paramName(shaperParamIndex(0, 0))) == "High Freq");
  REQUIRE(std::string(synth.paramName(shaperParamIndex(1, 0))) == "(unused)");
  char buf[64];
  synth.formatParam(filterParamIndex(0, 2), 2.5 / 3.0, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "HP");
  synth.formatParam(filterEngineParamIndex(0), 3, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "State Variable Filter");
}

TEST_CASE("Switching Filter type keeps the output aligned in time") {
  auto onset = [](double rate, int filter, int shaper) {
    Synth synth(PluginKind::Effect);
    synth.setParam(filterTypeId(0), filter);
    synth.setParam(shaperTypeId(0), shaper);
    synth.prepare(rate, kBlock);
    Stereo in(9600);
    in.l[4000] = in.r[4000] = 0.25f;
    Stereo out(9600);
    run(synth, out, 0, 9600, &in);
    size_t peak = 0;
    for (size_t i = 0; i < out.l.size(); ++i) {
      if (std::fabs(out.l[i]) > std::fabs(out.l[peak])) peak = i;
    }
    return std::make_pair(synth.latencySamples(), int(peak) - 4000);
  };
  for (double rate : {44100.0, 48000.0, 96000.0}) {
    const auto off = onset(rate, int(FilterEngine::Off), int(ShaperEngine::Off));
    INFO("rate " << rate);
    REQUIRE(off.second == off.first);  // Off and Off: exactly the reported latency
    // Only an engine's own group delay moves the peak: the Analog Filter's oversampling
    // filters delay it by ~0.12 ms (6 samples at 48 kHz, measured during planning; 11 at 96 kHz),
    // as in VCV. The bound is 8 samples at 48 kHz, kept in time at other rates.
    const int bound = int(std::ceil(8.0 * rate / 48000.0));
    for (int filter : {int(FilterEngine::Analog), int(FilterEngine::Classic), int(FilterEngine::StateVariable)}) {
      const auto with = onset(rate, filter, int(ShaperEngine::Off));
      INFO("filter " << filter << " latency " << with.first << " onset " << with.second);
      REQUIRE(with.first == off.first);  // the reported latency does not change with the type
      REQUIRE(std::abs(with.second - off.second) <= bound);
    }
    const auto eq = onset(rate, int(FilterEngine::Off), int(ShaperEngine::Eq));
    INFO("EQ latency " << eq.first << " onset " << eq.second);
    REQUIRE(eq.first == off.first);
    REQUIRE(std::abs(eq.second - off.second) <= bound);
  }
}

TEST_CASE("Switching Filter type while audio plays makes no step") {
  Synth synth(PluginKind::Effect);
  synth.setParam(filterTypeId(0), 0);
  synth.prepare(48000.0, kBlock);
  synth.setActive(true);
  const Stereo in = sineInput(24000, 48000.0);
  Stereo out(24000);
  run(synth, out, 0, 9600, &in);
  synth.setParam(filterTypeId(0), double(FilterEngine::StateVariable));  // open: nearly transparent
  run(synth, out, 9600, 24000, &in);
  float maxStep = 0.0f;
  for (size_t i = 4800; i < 24000; ++i) maxStep = std::max(maxStep, std::fabs(out.l[i] - out.l[i - 1]));
  REQUIRE(maxStep < 0.05f);  // a 220 Hz, 0.5 sine moves at most 0.0144 per sample
  synth.setActive(false);
}

namespace {

// What Part B does in renderRingMod: nothing, play its input always on, or play it while a note
// (sent to the synth) holds.
enum class PartB { Silent, AlwaysOn, Notes };

// EG13 FX with Part A's Ring Mod set to digital ring modulation, Carrier = Voice (Part A's
// Audio Input, a 1 kHz sine) and the given Modulator. Part B plays the same input through
// Audio Input as `partB` says, heard only by Part A.
std::vector<float> renderRingMod(PartMode mode, float modulator, PartB partB) {
  Synth synth(PluginKind::Effect);
  synth.setParam(masterId(MasterParam::Mode), double(mode));
  filtersOff(synth);
  synth.setParam(shaperTypeId(0), double(ShaperEngine::RingMod));
  synth.setParam(shaperId(0, 0), 0.375);  // digital ring modulation
  synth.setParam(shaperId(0, 1), 0.0);
  synth.setParam(shaperId(0, 2), 0.5);
  synth.setParam(shaperId(0, 3), 0.5);
  synth.setParam(shaperId(0, 4), WarpsEngine::optionValue(1));  // Carrier: Voice (Part A's input)
  synth.setParam(shaperId(0, 5), modulator);
  if (partB != PartB::Silent) {
    synth.setParam(paramSpec(sourceEngineParamIndex(1)).id, 4);
    synth.setParam(partId(1, PartParam::Trigger), partB == PartB::AlwaysOn ? 1 : 0);
    synth.setParam(partId(1, PartParam::Output), 2);
  }
  synth.prepare(48000.0, kBlock);
  if (partB == PartB::Notes) synth.noteOn(0, 60, 1.0f);  // Layer: both parts; Single: Part A only
  Stereo in(48000);
  for (size_t i = 0; i < in.l.size(); ++i) {
    in.l[i] = in.r[i] = 0.4f * float(std::sin(6.283185307179586 * 1000.0 * double(i) / 48000.0));
  }
  Stereo out(48000);
  run(synth, out, 0, 48000, &in);
  for (float s : out.l) REQUIRE(std::isfinite(s));
  return out.l;
}

double magnitudeAt(const std::vector<float>& x, double hz) {  // Goertzel from 9600 on
  const double c = 2.0 * std::cos(6.283185307179586 * hz / 48000.0);
  double s1 = 0.0, s2 = 0.0;
  for (size_t i = 9600; i < x.size(); ++i) {
    const double s0 = double(x[i]) + c * s1 - s2;
    s2 = s1;
    s1 = s0;
  }
  return std::sqrt(std::max(0.0, s1 * s1 + s2 * s2 - c * s1 * s2));
}

}  // namespace

TEST_CASE("The Ring Mod modulator takes the part's Input and the other part's output") {
  const auto input = renderRingMod(PartMode::Layer, WarpsEngine::optionValue(2), PartB::Silent);  // Input: carrier²
  REQUIRE(magnitudeAt(input, 2000.0) > 10.0 * magnitudeAt(input, 1000.0));
  const auto other = renderRingMod(PartMode::Layer, WarpsEngine::optionValue(3), PartB::AlwaysOn);  // Part B's copy
  REQUIRE(magnitudeAt(other, 2000.0) > 10.0 * magnitudeAt(other, 1000.0));
  const auto silentB = renderRingMod(PartMode::Layer, WarpsEngine::optionValue(3), PartB::Silent);  // B plays nothing
  REQUIRE(test::rms(silentB, 9600, silentB.size()) < 1e-4);
}

TEST_CASE("In Single mode the Ring Mod's Other part is silent unless Part B runs on its own") {
  // A note reaches Part B in Layer mode, so it plays and feeds Part A's Ring Mod...
  const auto layerB = renderRingMod(PartMode::Layer, WarpsEngine::optionValue(3), PartB::Notes);
  REQUIRE(magnitudeAt(layerB, 2000.0) > 10.0 * magnitudeAt(layerB, 1000.0));
  // ...but Single mode sends Part B no notes, so its output, and the Other part signal, is silent.
  const auto silentB = renderRingMod(PartMode::Single, WarpsEngine::optionValue(3), PartB::Notes);
  REQUIRE(test::rms(silentB, 9600, silentB.size()) < 1e-4);
  // An Always-on Part B still runs in Single mode (muted in the mix) and feeds Part A.
  const auto alwaysOnB = renderRingMod(PartMode::Single, WarpsEngine::optionValue(3), PartB::AlwaysOn);
  REQUIRE(magnitudeAt(alwaysOnB, 2000.0) > 10.0 * magnitudeAt(alwaysOnB, 1000.0));
}

namespace {

// EG13 FX, Layer mode, heard from Part B only: Part A plays the input (a 1 kHz sine of the given
// amplitude) and feeds Part B's Ring Mod as its Modulator (Other part, A -> B). Part B's own
// input is its Carrier (Voice), at the given Carrier Level.
std::vector<float> renderAToB(float amplitude, float algorithm, float timbre, float carrierLevel, float modLevel) {
  Synth synth(PluginKind::Effect);
  synth.setParam(masterId(MasterParam::Mode), double(PartMode::Layer));
  filtersOff(synth);
  synth.setParam(partId(0, PartParam::Output), 2);  // Part A: heard only by Part B
  synth.setParam(paramSpec(sourceEngineParamIndex(1)).id, 4);
  synth.setParam(partId(1, PartParam::Trigger), 1);
  synth.setParam(shaperTypeId(1), double(ShaperEngine::RingMod));
  synth.setParam(shaperId(1, 0), algorithm);
  synth.setParam(shaperId(1, 1), timbre);
  synth.setParam(shaperId(1, 2), carrierLevel);
  synth.setParam(shaperId(1, 3), modLevel);
  synth.setParam(shaperId(1, 4), WarpsEngine::optionValue(1));  // Carrier: Voice (Part B's input)
  synth.setParam(shaperId(1, 5), WarpsEngine::optionValue(3));  // Modulator: Other part (Part A)
  synth.prepare(48000.0, kBlock);
  Stereo in(48000);
  for (size_t i = 0; i < in.l.size(); ++i) {
    in.l[i] = in.r[i] = amplitude * float(std::sin(6.283185307179586 * 1000.0 * double(i) / 48000.0));
  }
  Stereo out(48000);
  run(synth, out, 0, 48000, &in);
  for (float x : out.l) REQUIRE(std::isfinite(x));
  return out.l;
}

}  // namespace

TEST_CASE("The Ring Mod takes the other part from A to B, soft-limited") {
  // Digital ring modulation of Part B's 1 kHz input by Part A's: the product at 2 kHz, no 1 kHz.
  const auto product = renderAToB(0.4f, 0.375f, 0.0f, 0.5f, 0.5f);
  REQUIRE(magnitudeAt(product, 2000.0) > 100.0 * magnitudeAt(product, 1000.0));
  // Crossfade at full Timbre passes the Modulator alone, linearly at a low Mod Level: so the
  // output's peak follows the Other part signal. A quiet Part A passes as it is; a loud one
  // (4.0, i.e. 16 times the quiet one) arrives soft-limited to 1 + 3/4 = 1.75 (7 times).
  auto peakOf = [](const std::vector<float>& x) {
    float m = 0.0f;
    for (size_t i = 9600; i < x.size(); ++i) m = std::max(m, std::fabs(x[i]));
    return m;
  };
  const float quiet = peakOf(renderAToB(0.25f, 0.0f, 1.0f, 0.0f, 0.2f));
  const float loud = peakOf(renderAToB(4.0f, 0.0f, 1.0f, 0.0f, 0.2f));
  REQUIRE(quiet > 0.01f);
  REQUIRE_THAT(double(loud / quiet), Catch::Matchers::WithinAbs(1.75 / 0.25, 0.1));
}

TEST_CASE("Filter and Shaper switching while playing does not allocate") {
  Synth synth(PluginKind::Instrument);
  synth.setParam(masterId(MasterParam::Mode), double(PartMode::Layer));
  synth.prepare(kRate, kBlock);
  synth.setActive(true);
  Stereo in = sineInput(kBlock, kRate), out(kBlock);
  run(synth, out, 0, kBlock, &in);
  synth.noteOn(0, 60, 0.8f);
  HostUpdate updates[kMaxHostUpdates];
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int block = 0; block < 120; ++block) {
      if (block % 10 == 0) {
        synth.setParam(filterTypeId(block % 20 == 0 ? 0 : 1), (block / 10) % kNumFilterEngines);
        synth.setParam(shaperTypeId(0), (block / 10) % kNumShaperEngines);
        synth.setParam(shaperId(0, 4), WarpsEngine::optionValue((block / 10) % WarpsEngine::kNumCarriers));
        synth.setParam(shaperId(0, 5), WarpsEngine::optionValue((block / 10) % WarpsEngine::kNumModulators));
      }
      run(synth, out, 0, kBlock, &in);
      synth.takeHostUpdates(updates, int(kMaxHostUpdates));
      synth.takeEngineChanged();
    }
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
  synth.setActive(false);
}

TEST_CASE("Switching a mod engine while notes sound swaps its P values and scope") {
  Synth synth(PluginKind::Instrument);
  synth.prepare(kRate, kBlock);
  synth.setActive(true);
  synth.noteOn(0, 60, 1.0f);
  runBlocks(synth, 2);
  HostUpdate updates[kMaxHostUpdates];
  synth.takeHostUpdates(updates, int(kMaxHostUpdates));
  const uint32_t engineId = paramSpec(modEngineParamIndex(0, 0)).id;
  const uint32_t scopeId = paramSpec(modScopeParamIndex(0, 0)).id;
  const uint32_t p4 = paramSpec(modParamIndex(0, 0, 3)).id;
  synth.setParam(p4, 0.8);  // the Envelope's Decay
  synth.setParam(engineId, double(ModEngineKind::Random));
  runBlocks(synth, 1);
  REQUIRE(synth.takeHostUpdates(updates, int(kMaxHostUpdates)) == int(kModSlotParamCount) + 1);
  REQUIRE(updates[int(kModSlotParamCount)].id == scopeId);
  REQUIRE(updates[int(kModSlotParamCount)].value == 1.0f);  // Random / S&H starts per part
  REQUIRE(synth.param(scopeId) == 1.0);
  REQUIRE(synth.param(p4) == double(modEngineInfo(int(ModEngineKind::Random)).engine.params[3].defaultValue));
  REQUIRE(std::string(synth.paramName(modParamIndex(0, 0, 0))) == "T Model");
  REQUIRE(synth.takeEngineChanged());
  synth.setParam(scopeId, 0.0);  // per voice
  synth.setParam(engineId, double(ModEngineKind::Envelope));
  runBlocks(synth, 1);
  REQUIRE(synth.param(p4) == double(0.8f));
  REQUIRE(synth.param(scopeId) == 0.0);
  synth.setParam(engineId, double(ModEngineKind::Random));
  REQUIRE(synth.param(scopeId) == 0.0);  // Random remembers the scope it was given
  REQUIRE(std::string(synth.paramName(modParamIndex(0, 0, 0))) == "T Model");
  char buf[64];
  synth.formatParam(modParamIndex(0, 0, 13), modOptionValue(1), buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Minor");
  synth.setActive(false);
}

TEST_CASE("Routes and parameters of a load apply in the same block") {
  Synth synth(PluginKind::Instrument);
  synth.prepare(kRate, kBlock);
  synth.setActive(true);
  runBlocks(synth, 1);
  synth.setRoute(0, 5, Route{kSrcGate, kDestPan, kSrcNone});  // an edit, not yet taken
  PatchData loaded = Synth::defaultPatch(PluginKind::Instrument);
  loaded.routes[1][2] = Route{kSrcRandom, uint16_t(kDestSourceP + 3), kSrcNone};
  ParamSnapshot values = synth.defaultSnapshot();
  values[routeAmountParamIndex(1, 2)] = -0.4f;
  synth.loadParams(values, nullptr, &loaded);  // supersedes the edit
  REQUIRE(synth.patch().routes[1][2] == loaded.routes[1][2]);
  REQUIRE(synth.appliedPatch().routes[1][2] == initPatchRoute(PluginKind::Instrument, 1, 2));  // not yet
  synth.beginBlock();
  REQUIRE(synth.appliedPatch().routes[1][2] == loaded.routes[1][2]);
  REQUIRE(synth.appliedPatch().routes[0][5] == Route{});  // the older edit was dropped
  REQUIRE(synth.param(paramSpec(routeAmountParamIndex(1, 2)).id) == double(-0.4f));
  synth.setRoute(0, 6, Route{kSrcNote, kDestPitch, kSrcNone});  // an edit after the load
  synth.beginBlock();
  REQUIRE(synth.appliedPatch().routes[0][6] == Route{kSrcNote, kDestPitch, kSrcNone});
  REQUIRE(synth.appliedPatch().routes[1][2] == loaded.routes[1][2]);
  // A load and an edit made right after it reach the audio thread in the same block, in order.
  synth.loadParams(values, nullptr, &loaded);
  synth.setRoute(0, 7, Route{kSrcGate, kDestAmp, kSrcNone});
  synth.beginBlock();
  REQUIRE(synth.appliedPatch().routes[0][7] == Route{kSrcGate, kDestAmp, kSrcNone});
  REQUIRE(synth.appliedPatch().routes[0][6] == Route{});  // the load replaced the earlier edit
  REQUIRE(synth.appliedPatch().routes[1][2] == loaded.routes[1][2]);
  synth.setActive(false);
}

TEST_CASE("A route amount's text follows the route's destination") {
  Synth synth(PluginKind::Instrument);
  char buf[64];
  synth.formatParam(routeAmountParamIndex(0, 2), 0.5 / 24.0, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "+0.50 st");  // the init patch's vibrato route
  synth.formatParam(routeAmountParamIndex(0, 1), 0.3, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "+30%");
  synth.formatParam(routeAmountParamIndex(0, 7), 0.3, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "-");
  REQUIRE_FALSE(synth.takeEngineChanged());
  synth.setRoute(0, 7, Route{kSrcVelocity, kDestPitch, kSrcNone});
  synth.formatParam(routeAmountParamIndex(0, 7), 0.25, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "+6.00 st");
  REQUIRE(synth.takeEngineChanged());  // the host is told to refresh the text
}

TEST_CASE("The instrument init patch: an envelope on the Amp, scaled by velocity, with a release") {
  // A steady 0.5 source (it ignores the gate), so only the Amp shapes the level.
  auto steady = [] { return SourceSpec{std::make_unique<test::ConstantEngine>(), 0.0, 16}; };
  auto play = [&steady](float velocity) {
    Synth synth(PluginKind::Instrument, steady);
    synth.setParam(masterId(MasterParam::SoftClip), 0.0);
    synth.prepare(kRate, kBlock);
    synth.noteOn(0, 57, velocity);
    Stereo out(52800);
    run(synth, out, 0, 24000);
    synth.noteOff(0, 57);
    run(synth, out, 24000, 52800);
    return out;
  };
  const Stereo loud = play(1.0f);
  const Stereo soft = play(0.5f);
  const double held = test::rms(loud.l, 4800, 24000);
  // 0.5 through the State Variable Filter's low-pass (opened by Mod 2), the Amp at velocity x
  // full sustain, -12 dB Voice Gain, 80 % master level.
  REQUIRE(std::fabs(held - 0.5 * 0.25119 * 0.8) < 0.002);
  REQUIRE(std::fabs(test::rms(soft.l, 4800, 24000) / held - 0.5) < 0.01);
  // Mod 1's release (0.5 s), not the gate's 5 ms: still sounding 0.1 s after the key.
  REQUIRE(test::rms(loud.l, 28800, 29280) > 0.25 * held);
  REQUIRE(test::rms(loud.l, 52320, 52800) < 0.01 * held);  // 0.6 s after the key
}

TEST_CASE("Voice Gain plays each voice 12 dB lower") {
  auto level = [](double voiceGain) {
    Synth synth(PluginKind::Instrument, sine);
    test::m3Patch(synth);
    synth.setParam(masterId(MasterParam::VoiceGain), voiceGain);
    synth.prepare(kRate, kBlock);
    synth.noteOn(0, 60, 1.0f);
    Stereo out(9600);
    run(synth, out, 0, 9600);
    return test::rms(out.l, 4800, 9600);
  };
  REQUIRE(std::fabs(level(0.0) / level(1.0) - 0.25119) < 1e-3);
}

TEST_CASE("Soft Clip keeps both outputs within full scale") {
  auto peak = [](bool softClip, bool bus2) {
    Synth synth(PluginKind::Instrument, sine);
    test::m3Patch(synth);  // 0 dB voices: an 8-note chord of 0.5 sines peaks near 3
    synth.setParam(masterId(MasterParam::Level), 1.0);
    synth.setParam(masterId(MasterParam::SoftClip), softClip ? 1.0 : 0.0);
    if (bus2) synth.setParam(partId(0, PartParam::Output), 1);
    synth.prepare(kRate, kBlock);
    for (int key = 60; key < 68; ++key) synth.noteOn(0, key, 1.0f);
    Stereo out(9600), busOut(9600);
    run(synth, out, 0, 9600, nullptr, &busOut);
    const Stereo& o = bus2 ? busOut : out;
    float p = 0.0f;
    for (size_t i = 0; i < o.l.size(); ++i) p = std::max(p, std::max(std::fabs(o.l[i]), std::fabs(o.r[i])));
    return p;
  };
  REQUIRE(peak(false, false) > 1.5f);
  REQUIRE(peak(true, false) <= 1.0f);
  REQUIRE(peak(true, false) > 0.9f);
  REQUIRE(peak(true, true) <= 1.0f);
}

TEST_CASE("Synth processing with every mod slot and route in use does not allocate") {
  Synth synth(PluginKind::Instrument);
  synth.setParam(masterId(MasterParam::Mode), double(PartMode::Layer));
  for (int part = 0; part < kNumParts; ++part) {
    for (int slot = 0; slot < kNumModSlots; ++slot) {
      synth.setParam(paramSpec(modEngineParamIndex(part, slot)).id, double(1 + slot));
    }
    for (int r = 0; r < kNumRoutes; ++r) {
      synth.setRoute(part, r, Route{modOutputSource(r % kNumModSlots, 0), uint16_t(r < 4 ? kDestSourceP + r : kDestPan), kSrcNone});
      synth.setParam(paramSpec(routeAmountParamIndex(part, r)).id, 0.2);
    }
  }
  synth.prepare(44100.0, kBlock);
  synth.setActive(true);
  Stereo out(kBlock);
  run(synth, out, 0, kBlock);
  test::ScopedAllocationCounter counter;
  for (int block = 0; block < 200; ++block) {
    if (block % 50 == 0) for (int key : {48, 55, 60, 64, 67}) synth.noteOn(0, key, 0.8f);
    if (block % 50 == 40) synth.allNotesOff();
    if (block == 120) synth.setParam(paramSpec(modEngineParamIndex(0, 1)).id, 3.0);
    if (block == 150) synth.setParam(masterId(MasterParam::VoiceGain), 1.0);
    run(synth, out, 0, kBlock);
  }
  const size_t allocations = counter.count();
  REQUIRE(allocations == 0);
  synth.setActive(false);
}

TEST_CASE("Switching mod engines and scopes while notes play does not allocate") {
  Synth synth(PluginKind::Instrument);
  synth.setParam(masterId(MasterParam::Mode), double(PartMode::Layer));
  synth.prepare(48000.0, kBlock);
  synth.setActive(true);
  Stereo out(kBlock);
  run(synth, out, 0, kBlock);
  for (int key : {48, 55, 60, 64, 67}) synth.noteOn(0, key, 0.8f);
  run(synth, out, 0, kBlock);
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int block = 0; block < 200; ++block) {
      if (block % 5 == 0) {
        const int step = block / 5;
        for (int part = 0; part < kNumParts; ++part) {
          for (int slot = 0; slot < kNumModSlots; ++slot) {
            synth.setParam(paramSpec(modEngineParamIndex(part, slot)).id, double((step + slot) % kNumModEngines));
            synth.setParam(paramSpec(modScopeParamIndex(part, slot)).id, double((step + part + slot) % 2));
          }
        }
      }
      if (block == 100) synth.noteOn(0, 72, 0.5f);
      run(synth, out, 0, kBlock);
    }
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
  synth.setActive(false);
}

namespace {

SourceSpec steadySource() { return {std::make_unique<test::ConstantEngine>(), 0.0, 16}; }
uint32_t modEngineId(int part, int slot) { return paramSpec(modEngineParamIndex(part, slot)).id; }
uint32_t modScopeId(int part, int slot) { return paramSpec(modScopeParamIndex(part, slot)).id; }
uint32_t modId(int part, int slot, int p) { return paramSpec(modParamIndex(part, slot, p)).id; }
uint32_t routeAmountId(int part, int r) { return paramSpec(routeAmountParamIndex(part, r)).id; }

// Mod 4 of Part A: an LFO per part (Sync on or off) into Part Level, -50 %.
void lfoOnPartLevel(Synth& synth, bool sync) {
  synth.setParam(modEngineId(0, 3), double(ModEngineKind::Lfo));
  synth.setParam(modScopeId(0, 3), 1.0);
  synth.setParam(modId(0, 3, 1), sync ? 1.0 : 0.0);  // Sync
  // Synced: one cycle per beat (the ratio table's centre); free: 9 Hz.
  synth.setParam(modId(0, 3, 2), sync ? 0.5 : double(TidesEngine::frequencyFor(9.0, TidesEngine::Medium)));
  synth.setRoute(0, 3, Route{modOutputSource(3, 0), kDestPartLevel, kSrcNone});
  synth.setParam(routeAmountId(0, 3), -0.5);
}

// Renders `total` samples in host blocks of `block`; before each block, `host(done)` may send
// events (the transport, notes).
Stereo renderHost(Synth& synth, size_t total, int block, const std::function<void(size_t)>& host) {
  Stereo out(total);
  for (size_t done = 0; done < total;) {
    const int n = int(std::min<size_t>(size_t(block), total - done));
    if (host) host(done);
    float* o[2] = {out.l.data() + done, out.r.data() + done};
    synth.process(nullptr, o, nullptr, n);
    done += size_t(n);
  }
  return out;
}

Transport playingAt(double beats, double tempo) {
  Transport t;
  t.playing = true;
  t.hasTempo = true;
  t.tempo = tempo;
  t.hasBeats = true;
  t.beats = beats;
  t.hasTimeSignature = true;
  return t;
}

}  // namespace

TEST_CASE("A Part Level routed from an LFO plays the same at any host block size") {
  // Preflight ruling G5, through the Synth (applySettings before every host block).
  const double rate = GENERATE(44100.0, 48000.0, 96000.0);
  CAPTURE(rate);
  const size_t total = size_t(rate / 2.0);
  auto render = [&](int block) {
    Synth synth(PluginKind::Instrument, sine);
    lfoOnPartLevel(synth, false);
    synth.prepare(rate, 512);
    synth.noteOn(0, 60, 1.0f);
    return renderHost(synth, total, block, nullptr);
  };
  const Stereo a = render(512);
  // The LFO moves the level: the loudest and quietest 10 ms differ by more than 3 dB.
  double low = 1.0, high = 0.0;
  for (size_t at = total / 2; at + size_t(rate / 100.0) < total; at += size_t(rate / 100.0)) {
    const double level = test::rms(a.l, at, at + size_t(rate / 100.0));
    low = std::min(low, level);
    high = std::max(high, level);
  }
  REQUIRE(high > 1.41 * low);
  for (int block : {1, 7, 100}) {
    INFO("block " << block);
    const Stereo b = render(block);
    REQUIRE(a.l == b.l);
    REQUIRE(a.r == b.r);
  }
}

TEST_CASE("A tempo-synced LFO plays the same at any host block size") {
  // The beat is counted from absolute samples, so every tick's beat is bit-identical whatever the
  // host's block sizes: free-running with the transport stopped, and following a playing host
  // that reports its position every block.
  const double rate = GENERATE(44100.0, 48000.0, 96000.0);
  const bool playing = GENERATE(false, true);
  CAPTURE(rate, playing);
  const size_t total = size_t(rate / 2.0);
  const double tempo = 137.0;
  auto render = [&](int block) {
    Synth synth(PluginKind::Instrument, sine);
    lfoOnPartLevel(synth, true);
    synth.prepare(rate, 512);
    synth.noteOn(0, 60, 1.0f);
    return renderHost(synth, total, block, [&](size_t done) {
      Transport t = playingAt(5.25 + double(done) * tempo / 60.0 / rate, tempo);
      t.playing = playing;
      synth.setTransport(t);
    });
  };
  const Stereo a = render(512);
  for (int block : {1, 7, 100}) {
    INFO("block " << block);
    const Stereo b = render(block);
    REQUIRE(a.l == b.l);
    REQUIRE(a.r == b.r);
  }
}

TEST_CASE("Transport Phase free-runs at the last tempo while stopped and locks when the transport starts") {
  // Transport Phase into the Amp of a steady 0.5 source: the output is 0.5 x the bar phase, one
  // voice latency later.
  Synth synth(PluginKind::Instrument, steadySource);
  test::m3Patch(synth);
  synth.setParam(masterId(MasterParam::Level), 1.0);
  synth.setRoute(0, 0, Route{kSrcTransportPhase, kDestAmp, kSrcNone});
  synth.setParam(routeAmountId(0, 0), 1.0);
  synth.prepare(kRate, kBlock);
  const size_t latency = size_t(synth.latencySamples());
  synth.noteOn(0, 60, 1.0f);
  Transport stopped;
  Transport t = playingAt(10.0, 120.0);  // 2.5 bars of 4/4: halfway through a bar
  const size_t start = 48000;            // the transport starts after 1 s
  const Stereo out = renderHost(synth, 96000, 64, [&](size_t done) {
    if (done < 24000) return;  // no transport yet: 120 BPM
    if (done < start) {
      stopped.hasTempo = true;  // stopped, at 60 BPM: a bar every 4 s
      stopped.tempo = 60.0;
      synth.setTransport(stopped);
    } else {
      t.beats = 10.0 + double(done - start) * 2.0 / kRate;
      synth.setTransport(t);
    }
  });
  auto phaseAt = [&](size_t i) { return double(out.l[i + latency]) / 0.5; };
  // 120 BPM from the start: 0.5 s is a quarter of a bar.
  REQUIRE(std::fabs(phaseAt(12000) - 0.125) < 0.005);
  REQUIRE(std::fabs(phaseAt(24000) - 0.25) < 0.005);
  // Stopped at 60 BPM: it keeps running, a quarter as fast per second as a 4/4 bar at 120 BPM.
  REQUIRE(std::fabs(phaseAt(36000) - (0.25 + 0.0625)) < 0.005);
  REQUIRE(std::fabs(phaseAt(start - 64) - (0.375 - 64.0 / kRate / 4.0)) < 0.005);
  // The transport plays from beat 10: it jumps into line with the song.
  REQUIRE(std::fabs(phaseAt(start + 32) - (0.5 + 32.0 / kRate / 2.0)) < 0.005);
  REQUIRE(std::fabs(phaseAt(start + 24000) - 0.75) < 0.005);
}

TEST_CASE("A synced LFO started from a stopped transport lines up with the song") {
  // Owner decision 4: stopped, a synced LFO free-runs at the last tempo; when the transport
  // starts it locks to the song. After the start it plays as one that played from the song
  // position all along, to within about one sample of the LFO's own rate.
  const double rate = GENERATE(44100.0, 48000.0, 96000.0);
  CAPTURE(rate);
  const double tempo = 100.0;
  const size_t start = size_t(rate * 0.3) / 64 * 64;  // a host block boundary
  const size_t length = size_t(rate * 0.5);
  auto render = [&](size_t stoppedFor) {
    Synth synth(PluginKind::Instrument, steadySource);
    test::m3Patch(synth);
    synth.setParam(partId(0, PartParam::Trigger), 1);  // Always on: a steady voice, no notes
    lfoOnPartLevel(synth, true);
    synth.prepare(rate, 512);
    return renderHost(synth, stoppedFor + length, 64, [&](size_t done) {
      Transport t = playingAt(8.5 + (double(done) - double(stoppedFor)) * tempo / 60.0 / rate, 90.0);
      if (done < stoppedFor) {
        t.playing = false;  // stopped: 90 BPM, a different tempo and phase from the song's
      } else {
        t.tempo = tempo;
      }
      synth.setTransport(t);
    });
  };
  const Stereo fromStop = render(start);
  const Stereo fromStart = render(0);
  // The two differ before the start, and match after it once the level has settled.
  double before = 0.0, after = 0.0;
  for (size_t i = size_t(rate * 0.1); i < start; ++i) before = std::max(before, double(std::fabs(fromStop.l[i] - fromStart.l[i])));
  for (size_t i = size_t(rate * 0.1); i < length; ++i) {
    after = std::max(after, double(std::fabs(fromStop.l[start + i] - fromStart.l[i])));
  }
  CAPTURE(before, after);
  // Before the start the stopped render runs half a cycle away from the song (the song starts on
  // beat 8.5, the stopped clock from beat 0, at a different tempo). Measured 0.147 to 0.148 at
  // each rate, against an output swing of about 0.19 for a full LFO cycle; 0.1 shows they are
  // clearly out of line.
  REQUIRE(before > 0.1);
  REQUIRE(after < 1e-3);
}

TEST_CASE("An Always-on part's envelope on the Amp follows the keys, the first key triggering") {
  // The instrument init patch (Mod 1's envelope on the Amp) with Trigger Always on, a steady 0.5
  // source and a percussive envelope (no sustain).
  Synth synth(PluginKind::Instrument, steadySource);
  synth.setParam(partId(0, PartParam::Trigger), 1);
  synth.setParam(modId(0, 0, 3), 0.1);  // Decay
  synth.setParam(modId(0, 0, 5), 0.0);  // Sustain
  synth.prepare(kRate, kBlock);
  Stereo out(48000);
  run(synth, out, 0, 4800);
  REQUIRE(synth.activeVoiceCount(0) == 1);  // the voice is on, its Amp closed
  REQUIRE(test::rms(out.l, 2400, 4800) < 1e-4);
  synth.noteOn(0, 60, 1.0f);  // the first key triggers the envelope
  run(synth, out, 4800, 14400);
  const double struck = test::rms(out.l, 4800, 6000);
  REQUIRE(struck > 0.01);
  REQUIRE(test::rms(out.l, 12000, 14400) < 0.01 * struck);  // decayed
  synth.noteOn(0, 64, 1.0f);  // a second key while the first is held: no retrigger
  run(synth, out, 14400, 24000);
  REQUIRE(test::rms(out.l, 14400, 24000) < 0.01 * struck);
  synth.noteOff(0, 60);
  synth.noteOff(0, 64);
  run(synth, out, 24000, 33600);
  synth.noteOn(0, 67, 1.0f);  // after every key was up: the next key triggers again
  run(synth, out, 33600, 48000);
  REQUIRE(test::rms(out.l, 33600, 34800) > 0.5 * struck);
}

TEST_CASE("The instrument plays the Modal Synth and Resonator at their trimmed level less 12 dB") {
  // Their +12 dB Source trims are matched by the default -12 dB Voice Gain (rulings 24 and 25).
  for (const SourceEngine engine : {SourceEngine::Modal, SourceEngine::Resonator}) {
    auto level = [engine](double voiceGain) {
      Synth synth(PluginKind::Instrument);
      synth.setParam(paramSpec(sourceEngineParamIndex(0)).id, double(int(engine)));
      synth.setParam(masterId(MasterParam::SoftClip), 0.0);
      if (voiceGain >= 0.0) synth.setParam(masterId(MasterParam::VoiceGain), voiceGain);
      synth.prepare(kRate, kBlock);
      synth.noteOn(0, 48, 1.0f);
      Stereo out(9600);
      run(synth, out, 0, 9600);
      return test::rms(out.l, 0, 9600);
    };
    INFO("engine " << int(engine));
    const double byDefault = level(-1.0);
    REQUIRE(byDefault > 0.0);
    REQUIRE(byDefault == level(0.0));  // the instrument's default: -12 dB
    REQUIRE(std::fabs(byDefault / level(1.0) - 0.25119) < 1e-3);
  }
}

TEST_CASE("Each part has its own random seeds") {
  // Both parts play the same patch, with Mod 4 a Random / S&H (per part) into Part Level, and
  // the per-note Random into pan; Part B goes to Bus 2. The same seeds would make them identical.
  Synth synth(PluginKind::Instrument, sine);
  test::m3Patch(synth);
  synth.setParam(masterId(MasterParam::Mode), double(PartMode::Layer));
  synth.setParam(partId(1, PartParam::Output), 1);
  for (int part = 0; part < kNumParts; ++part) {
    synth.setParam(modEngineId(part, 3), double(ModEngineKind::Random));
    synth.setRoute(part, 0, Route{modOutputSource(3, 3), kDestPartLevel, kSrcNone});  // X1
    synth.setParam(routeAmountId(part, 0), -0.5);
    synth.setRoute(part, 1, Route{kSrcRandom, kDestPan, kSrcNone});
    synth.setParam(routeAmountId(part, 1), 0.5);
  }
  synth.prepare(kRate, kBlock);
  synth.noteOn(0, 60, 1.0f);
  // The Random / S&H's first new value reaches Part Level at about 2 s, plus the part's latency
  // (Part FX included, M5): render 3 s.
  Stereo a(144000), b(144000);
  run(synth, a, 0, 144000, nullptr, &b);
  REQUIRE(test::rms(a.r, 4800, 96000) > 0.01);  // Random pans each part right of centre
  REQUIRE(test::rms(b.r, 4800, 96000) > 0.01);
  REQUIRE(a.l != b.l);
  REQUIRE(a.r != b.r);
}

TEST_CASE("Loads and route edits racing the audio thread: the newest edit wins, a load's patch comes with its values") {
  // Load k carries Route{Velocity, k} in Part B route 1 and k / 16384 in that route's amount;
  // the route edit made right after it sets Part A route 1 to Route{Gate, k}. On the audio
  // thread, after every block: Part B's route and amount name the same load, Part A's route is
  // that load's edit or not yet applied (0), and (load, edit) never goes backwards.
  constexpr int kLoads = 16000;
  Synth synth(PluginKind::Instrument);
  const size_t amountIndex = routeAmountParamIndex(1, 0);
  const ParamSnapshot defaults = synth.defaultSnapshot();
  {
    ParamSnapshot values = defaults;
    values[amountIndex] = 0.0f;
    const PatchData empty;
    synth.loadParams(values, nullptr, &empty);  // load 0, while inactive
  }
  synth.setActive(true);
  std::atomic<bool> done{false};
  std::atomic<int> mismatched{0}, backwards{0}, foreign{0};
  std::atomic<long> blocks{0};
  std::thread audio([&] {
    int lastLoad = 0, lastEdit = 0;
    for (;;) {
      const bool finished = done.load(std::memory_order_acquire);
      synth.beginBlock();
      const PatchData& patch = synth.appliedPatch();
      const int load = patch.routes[1][0].destination;
      const int value = int(std::lround(double(synth.appliedValue(amountIndex)) * 16384.0));
      const int edit = patch.routes[0][0].destination;
      if (load != value) mismatched.fetch_add(1, std::memory_order_relaxed);
      if (edit != 0 && edit != load) foreign.fetch_add(1, std::memory_order_relaxed);
      const int editFlag = edit != 0 ? 1 : 0;
      if (load < lastLoad || (load == lastLoad && editFlag < lastEdit)) backwards.fetch_add(1, std::memory_order_relaxed);
      lastLoad = load;
      lastEdit = editFlag;
      blocks.fetch_add(1, std::memory_order_relaxed);
      if (finished) break;
    }
  });
  for (int k = 1; k <= kLoads; ++k) {
    PatchData patch;
    patch.routes[1][0] = Route{kSrcVelocity, uint16_t(k), kSrcNone};
    ParamSnapshot values = defaults;
    values[amountIndex] = float(k) / 16384.0f;
    synth.loadParams(values, nullptr, &patch);
    synth.setRoute(0, 0, Route{kSrcGate, uint16_t(k), kSrcNone});
  }
  done.store(true, std::memory_order_release);
  audio.join();
  synth.beginBlock();  // whatever was still waiting
  INFO("blocks run: " << blocks.load());
  REQUIRE(blocks.load() > 0);
  REQUIRE(mismatched.load() == 0);
  REQUIRE(foreign.load() == 0);
  REQUIRE(backwards.load() == 0);
  REQUIRE(synth.appliedPatch().routes[0][0] == Route{kSrcGate, uint16_t(kLoads), kSrcNone});
  REQUIRE(synth.appliedPatch().routes[1][0] == Route{kSrcVelocity, uint16_t(kLoads), kSrcNone});
  REQUIRE(synth.appliedValue(amountIndex) == float(kLoads) / 16384.0f);
  synth.setActive(false);
}

TEST_CASE("The mod wheel brings in the init patch's vibrato") {
  // Per-cycle frequency range of a sine note over half a second, with the wheel at `wheel`.
  auto spread = [](int wheel) {
    Synth synth(PluginKind::Instrument, sine);
    synth.prepare(kRate, kBlock);
    synth.controlChange(0, 1, wheel);
    synth.noteOn(0, 69, 1.0f);
    Stereo out(48000);
    run(synth, out, 0, 48000);
    double lowest = 1e9, highest = 0.0, last = -1.0;
    for (size_t i = 24000; i < out.l.size(); ++i) {
      if (out.l[i - 1] < 0.0f && out.l[i] >= 0.0f) {
        const double t = double(i - 1) + double(-out.l[i - 1]) / double(out.l[i] - out.l[i - 1]);
        if (last >= 0.0) {
          lowest = std::min(lowest, kRate / (t - last));
          highest = std::max(highest, kRate / (t - last));
        }
        last = t;
      }
    }
    return highest / lowest;
  };
  REQUIRE(spread(0) < 1.002);
  const double full = spread(127);  // +-0.5 semitone: a whole semitone from lowest to highest
  REQUIRE(full > 1.04);
  REQUIRE(full < 1.08);
}

TEST_CASE("MPE member messages reach only their own voice; the master channel reaches the part") {
  Synth synth(PluginKind::Instrument, sine);
  test::m3Patch(synth);
  synth.setParam(partId(0, PartParam::MpeMode), 1);  // lower zone: master 1, members 2..16
  for (int p = 0; p < 3; ++p) synth.setParam(paramSpec(sourceParamIndex(0, p)).id, 0.0);  // routes add to 0
  synth.setRoute(0, 0, Route{kSrcChannelPressure, uint16_t(kDestSourceP + 1), kSrcNone});
  synth.setParam(paramSpec(routeAmountParamIndex(0, 0)).id, 1.0);
  synth.prepare(kRate, kBlock);
  Stereo out(kBlock);
  run(synth, out, 0, kBlock);
  synth.noteOn(1, 60, 1.0f);  // MIDI channel 2
  synth.noteOn(2, 64, 1.0f);  // MIDI channel 3
  synth.pitchBend(1, 0.5f);   // member: that note only, x 48 semitones
  synth.channelPressure(0, 0.6f);  // master: the whole part
  run(synth, out, 0, kBlock);
  const Part& a = synth.part(0);
  REQUIRE(a.lastControl(0).pitch == 24.0f);
  REQUIRE(a.lastControl(1).pitch == 0.0f);
  REQUIRE(a.lastControl(0).params.source.p[1] == 0.6f);
  REQUIRE(a.lastControl(1).params.source.p[1] == 0.6f);
  synth.channelPressure(2, 0.3f);  // member: that note's Pressure, not the part's
  run(synth, out, 0, kBlock);
  REQUIRE(a.lastControl(1).params.source.p[1] == 0.6f);
  synth.noteOn(5, 72, 1.0f);  // channel 6: another member
  REQUIRE(synth.activeVoiceCount(0) == 3);
}

TEST_CASE("Both parts in MPE split the member channels") {
  Synth synth(PluginKind::Instrument, sine);
  synth.setParam(masterId(MasterParam::Mode), double(PartMode::Multi));
  synth.setParam(partId(0, PartParam::MpeMode), 1);  // lower zone: channels 2..8
  synth.setParam(partId(1, PartParam::MpeMode), 2);  // upper zone: channels 9..15
  synth.prepare(kRate, kBlock);
  Stereo out(kBlock);
  run(synth, out, 0, kBlock);
  synth.noteOn(2, 60, 1.0f);  // channel 3: Part A
  synth.noteOn(9, 62, 1.0f);  // channel 10: Part B
  synth.noteOn(7, 64, 1.0f);  // channel 8: Part A
  synth.noteOn(8, 65, 1.0f);  // channel 9: Part B
  run(synth, out, 0, kBlock);
  REQUIRE(synth.activeVoiceCount(0) == 2);
  REQUIRE(synth.activeVoiceCount(1) == 2);
  synth.setParam(partId(1, PartParam::MpeMode), 0);  // Part B off: Part A's zone grows to 2..16
  run(synth, out, 0, kBlock);
  REQUIRE(synth.part(0).isMemberChannel(14));
}

TEST_CASE("CC A-D read their controller numbers and can learn a new one") {
  Synth synth(PluginKind::Instrument, sine);
  test::m3Patch(synth);
  synth.setParam(partId(0, PartParam::CcA), 20);
  for (int p = 0; p < 3; ++p) synth.setParam(paramSpec(sourceParamIndex(0, p)).id, 0.0);  // routes add to 0
  synth.setRoute(0, 0, Route{kSrcCcA, uint16_t(kDestSourceP + 0), kSrcNone});
  synth.setRoute(0, 1, Route{kSrcCcB, uint16_t(kDestSourceP + 1), kSrcNone});
  synth.setRoute(0, 2, Route{kSrcSlide, uint16_t(kDestSourceP + 2), kSrcNone});
  for (int r = 0; r < 3; ++r) synth.setParam(paramSpec(routeAmountParamIndex(0, r)).id, 1.0);
  synth.prepare(kRate, kBlock);
  synth.setActive(true);
  synth.noteOn(0, 60, 1.0f);
  synth.controlChange(0, 20, 127);
  synth.controlChange(0, 74, 127);  // not MPE: the part's Slide
  Stereo out(kBlock);
  run(synth, out, 0, kBlock);
  REQUIRE(synth.part(0).lastControl(0).params.source.p[0] == 1.0f);
  REQUIRE(synth.part(0).lastControl(0).params.source.p[2] == 1.0f);
  HostUpdate updates[kMaxHostUpdates];
  synth.takeHostUpdates(updates, int(kMaxHostUpdates));
  synth.takeEngineChanged();
  synth.learnCc(0, 1);  // CC B learns the next controller
  synth.controlChange(0, 64, 127);  // sustain is never learnt
  synth.controlChange(0, 96, 127);  // nor data increment and decrement (RPN/NRPN)
  synth.controlChange(0, 97, 127);
  REQUIRE(synth.param(partId(0, PartParam::CcB)) != 96.0);
  REQUIRE(synth.param(partId(0, PartParam::CcB)) != 97.0);
  synth.controlChange(0, 33, 127);
  REQUIRE(synth.param(partId(0, PartParam::CcB)) == 33.0);
  REQUIRE(synth.takeHostUpdates(updates, int(kMaxHostUpdates)) == 1);
  REQUIRE(updates[0].id == partId(0, PartParam::CcB));
  REQUIRE(updates[0].value == 33.0f);
  REQUIRE(synth.takeEngineChanged());
  synth.controlChange(0, 34, 127);  // the learn was used up
  REQUIRE(synth.param(partId(0, PartParam::CcB)) == 33.0);
  run(synth, out, 0, kBlock);
  REQUIRE(synth.part(0).lastControl(0).params.source.p[1] == 1.0f);
  synth.setActive(false);
}

TEST_CASE("Polyphonic pressure, note expressions and release velocity reach their notes") {
  Synth synth(PluginKind::Instrument, sine);
  test::m3Patch(synth);
  for (int p = 0; p < 3; ++p) synth.setParam(paramSpec(sourceParamIndex(0, p)).id, 0.0);  // routes add to 0
  synth.setRoute(0, 0, Route{kSrcPressure, uint16_t(kDestSourceP + 0), kSrcNone});
  synth.setRoute(0, 1, Route{kSrcReleaseVelocity, uint16_t(kDestSourceP + 1), kSrcNone});
  synth.setParam(paramSpec(routeAmountParamIndex(0, 0)).id, 1.0);
  synth.setParam(paramSpec(routeAmountParamIndex(0, 1)).id, 1.0);
  synth.prepare(kRate, kBlock);
  synth.noteOn(0, 60, 1.0f);
  synth.noteOn(0, 64, 1.0f);
  synth.polyPressure(0, 64, 0.5f);
  synth.noteExpression(0, 60, Expression::Bend, 3.0f);
  Stereo out(kBlock);
  run(synth, out, 0, kBlock);
  const Part& a = synth.part(0);
  REQUIRE(a.lastControl(0).params.source.p[0] == 0.0f);
  REQUIRE(a.lastControl(1).params.source.p[0] == 0.5f);
  REQUIRE(a.lastControl(0).pitch == 3.0f);
  synth.noteOff(0, 64, 0.25f);
  run(synth, out, 0, kBlock);
  REQUIRE(a.lastControl(1).params.source.p[1] == 0.25f);
  synth.noteOn(0, 67, 1.0f);  // voice 2, the first free one (voice 1 is still releasing)
  synth.noteOn(0, 67, 0.0f);  // a note-on at velocity 0: a note-off without release velocity
  run(synth, out, 0, kBlock);
  REQUIRE(a.lastControl(2).params.source.p[1] == kDefaultReleaseVelocity);
}

TEST_CASE("A note expression with key -1 reaches every note (a CLAP wildcard)") {
  Synth synth(PluginKind::Instrument, sine);
  test::m3Patch(synth);
  synth.setParam(paramSpec(sourceParamIndex(0, 0)).id, 0.0);  // the route adds to 0
  synth.setRoute(0, 0, Route{kSrcPressure, uint16_t(kDestSourceP + 0), kSrcNone});
  synth.setParam(paramSpec(routeAmountParamIndex(0, 0)).id, 1.0);
  synth.prepare(kRate, kBlock);
  synth.noteOn(0, 60, 1.0f);
  synth.noteOn(0, 64, 1.0f);
  synth.noteExpression(-1, -1, Expression::Pressure, 0.5f);  // any channel, any key
  Stereo out(kBlock);
  run(synth, out, 0, kBlock);
  const Part& a = synth.part(0);
  REQUIRE(a.lastControl(0).params.source.p[0] == 0.5f);
  REQUIRE(a.lastControl(1).params.source.p[0] == 0.5f);
  synth.noteExpression(0, -1, Expression::Pressure, 0.25f);  // any key on channel 1
  synth.noteExpression(3, -1, Expression::Pressure, 1.0f);   // no note on channel 4
  run(synth, out, 0, kBlock);
  REQUIRE(a.lastControl(0).params.source.p[0] == 0.25f);
  REQUIRE(a.lastControl(1).params.source.p[0] == 0.25f);
}

TEST_CASE("A controller with no learn pending leaves CC A alone") {
  Synth synth(PluginKind::Instrument, sine);
  synth.prepare(kRate, kBlock);
  HostUpdate updates[kMaxHostUpdates];
  synth.takeHostUpdates(updates, int(kMaxHostUpdates));
  synth.takeEngineChanged();
  const double before = synth.param(partId(0, PartParam::CcA));
  synth.controlChange(0, 33, 127);
  REQUIRE(synth.param(partId(0, PartParam::CcA)) == before);
  REQUIRE(synth.takeHostUpdates(updates, int(kMaxHostUpdates)) == 0);
  REQUIRE_FALSE(synth.takeEngineChanged());
}

TEST_CASE("Single mode ignores Part B's zone; the same zone twice goes to Part A") {
  Synth synth(PluginKind::Instrument, sine);
  synth.setParam(partId(0, PartParam::MpeMode), 1);
  synth.setParam(partId(1, PartParam::MpeMode), 2);  // Part B is off in Single mode
  synth.prepare(kRate, kBlock);
  Stereo out(kBlock);
  run(synth, out, 0, kBlock);
  REQUIRE(synth.part(0).isMemberChannel(14));
  synth.setParam(masterId(MasterParam::Mode), double(PartMode::Multi));
  run(synth, out, 0, kBlock);
  REQUIRE_FALSE(synth.part(0).isMemberChannel(14));
  synth.setParam(partId(1, PartParam::MpeMode), 1);  // both want the lower zone
  run(synth, out, 0, kBlock);
  REQUIRE(synth.part(0).isMemberChannel(14));
  synth.noteOn(2, 60, 1.0f);
  run(synth, out, 0, kBlock);
  REQUIRE(synth.activeVoiceCount(0) == 1);
  REQUIRE(synth.activeVoiceCount(1) == 0);
}

TEST_CASE("Learning skips MPE member channels and reserved controllers, and stays armed") {
  Synth synth(PluginKind::Instrument, sine);
  synth.setParam(partId(0, PartParam::MpeMode), 1);
  synth.prepare(kRate, kBlock);
  Stereo out(kBlock);
  run(synth, out, 0, kBlock);
  synth.learnCc(0, 1);
  synth.controlChange(3, 33, 127);  // a member channel
  for (int cc : {0, 32, 6, 38, 64, 98, 99, 100, 101, 120, 121, 123, 127}) synth.controlChange(0, cc, 127);
  REQUIRE(synth.param(partId(0, PartParam::CcB)) == 4.0);  // still the default
  synth.controlChange(0, 33, 127);  // the master channel
  REQUIRE(synth.param(partId(0, PartParam::CcB)) == 33.0);
}

// ---- M5: the Synth runs Part FX ----

namespace {

constexpr int kGranular = int(PartFxEngine::Granular);
constexpr int kReverb = int(PartFxEngine::Reverb);

// A mono sine burst (the reverb listens to left + right): `burst` samples of 220 Hz at 0.5, then
// silence, up to n.
Stereo burstInput(size_t n, size_t burst, double rate) {
  Stereo s = sineInput(n, rate);
  for (size_t i = 0; i < n; ++i) s.r[i] = s.l[i] = i < burst ? s.l[i] : 0.0f;
  return s;
}

double diffRms(const std::vector<float>& a, const std::vector<float>& b, size_t from, size_t to) {
  double sum = 0.0;
  for (size_t i = from; i < to; ++i) sum += double(a[i] - b[i]) * double(a[i] - b[i]);
  return std::sqrt(sum / double(to - from));
}

}  // namespace

TEST_CASE("Switching Part FX keeps each engine's settings") {
  Synth synth(PluginKind::Effect);
  synth.prepare(kRate, kBlock);
  synth.setActive(true);
  runBlocks(synth, 1);
  REQUIRE(synth.engine(SlotKind::PartFx, 0) == kGranular);
  synth.setParam(partFxId(0, 1), 0.2);  // Granular P2
  HostUpdate updates[kMaxHostUpdates];
  synth.takeHostUpdates(updates, int(kMaxHostUpdates));  // clear

  synth.setParam(partFxTypeId(0), kReverb);
  runBlocks(synth, 1);
  REQUIRE(synth.engine(SlotKind::PartFx, 0) == kReverb);
  const EngineInfo& reverb = partFxEngineInfo(kReverb);
  REQUIRE(synth.param(partFxId(0, 1)) == double(reverb.params[1].defaultValue));
  const int count = synth.takeHostUpdates(updates, int(kMaxHostUpdates));
  REQUIRE(count == int(kPartFxSlotParamCount));  // exactly the 16 Part FX P's, no other part
  for (int p = 0; p < int(kPartFxSlotParamCount); ++p) {
    REQUIRE(updates[p].id == partFxId(0, p));
    REQUIRE(updates[p].value == (p < reverb.paramCount ? reverb.params[p].defaultValue : 0.0f));
  }
  // The engine stash holds Granular's P2 while Reverb is current.
  REQUIRE(synth.engineStash()[partFxStashIndex(0, kGranular, 1)] == 0.2f);

  synth.setParam(partFxId(0, 1), 0.7);  // Reverb P2
  synth.setParam(partFxTypeId(0), kGranular);
  runBlocks(synth, 1);
  REQUIRE(synth.param(partFxId(0, 1)) == double(0.2f));
  REQUIRE(synth.engineStash()[partFxStashIndex(0, kReverb, 1)] == 0.7f);
  // Part B's Part FX is its own slot.
  REQUIRE(synth.engine(SlotKind::PartFx, 1) == int(PartFxEngine::Off));
  REQUIRE(synth.engineStash()[partFxStashIndex(1, kGranular, 1)] != 0.2f);
  synth.setActive(false);
}

TEST_CASE("A route into Part FX P1 changes the Granular blend") {
  auto render = [](bool route, int wheel) {
    Synth synth(PluginKind::Effect);
    test::m3Patch(synth);  // Part FX Off, no routes
    synth.setParam(filterTypeId(0), 0);
    synth.setParam(partFxTypeId(0), kGranular);
    synth.setParam(partFxId(0, 0), 0.0);  // Mix: dry
    if (route) {
      synth.setRoute(0, 0, Route{kSrcModWheel, uint16_t(kDestPartFxP), kSrcNone});
      synth.setParam(paramSpec(routeAmountParamIndex(0, 0)).id, 1.0);
    }
    synth.prepare(kRate, kBlock);
    synth.controlChange(0, 1, wheel);
    const Stereo in = sineInput(24000, kRate);
    Stereo out(24000);
    run(synth, out, 0, 24000, &in);
    return out.l;
  };
  const auto plain = render(false, 0);
  const auto wheelDown = render(true, 0);
  const auto wheelUp = render(true, 127);
  REQUIRE(diffRms(plain, wheelDown, 0, 24000) < 1e-6);  // the route adds nothing at 0
  const double moved = diffRms(plain, wheelUp, 9600, 24000);
  INFO("RMS change with the wheel up " << moved);
  REQUIRE(moved > 0.05);  // Mix 0 -> 100 % wet: a different sound
}

TEST_CASE("Other part hears the post-FX output") {
  // Part A: Audio Input, Reverb fully wet, heard only by Part B. Part B takes A as its Input.
  auto tailRms = [](int typeA) {
    Synth synth(PluginKind::Effect);
    test::m3Patch(synth);
    synth.setParam(filterTypeId(0), 0);
    synth.setParam(masterId(MasterParam::Mode), double(PartMode::Layer));
    synth.setParam(partId(0, PartParam::Output), 2);
    synth.setParam(partFxTypeId(0), typeA);
    synth.setParam(partFxId(0, 0), 1.0);  // Mix: wet
    synth.setParam(engineId(1), 4);       // Part B: Audio Input
    synth.setParam(partId(1, PartParam::Trigger), 1);
    synth.setParam(partId(1, PartParam::InputSource), 1);  // Other part
    synth.prepare(kRate, kBlock);
    const size_t total = 24000;
    const Stereo in = burstInput(total, 4800, kRate);
    Stereo out(total);
    run(synth, out, 0, total, &in);
    return test::rms(out.l, 14400, total);  // long after the burst and every latency
  };
  const double reverbTail = tailRms(kReverb);
  const double offTail = tailRms(int(PartFxEngine::Off));
  INFO("tail with Reverb " << reverbTail << ", with Off " << offTail);
  REQUIRE(offTail < 1e-4);
  REQUIRE(reverbTail > 1e-3);
}

TEST_CASE("Two parts feeding each other through Granular stay bounded") {
  Synth synth(PluginKind::Effect);
  synth.setParam(masterId(MasterParam::Mode), double(PartMode::Layer));
  synth.setParam(masterId(MasterParam::SoftClip), 0.0);  // the master clipper would bound it for us
  for (int part = 0; part < kNumParts; ++part) {
    synth.setParam(engineId(part), 0);  // Macro Oscillator, always on: the loop has energy
    synth.setParam(partId(part, PartParam::Trigger), 1);
    synth.setParam(filterTypeId(part), 0);
    synth.setParam(partFxTypeId(part), kGranular);
    synth.setParam(partId(part, PartParam::PartFxInput), 1);   // Input
    synth.setParam(partId(part, PartParam::InputSource), 1);   // Other part
    synth.setParam(partFxId(part, 0), 1.0);                    // Mix: wet
    synth.setParam(partFxId(part, 6), 1.0);                    // Feedback 1
  }
  synth.prepare(32000.0, 512);
  const size_t total = 32000 * 60;
  const size_t block = 512;
  std::vector<float> outL(block), outR(block);
  float peak = 0.0f;
  bool finite = true;
  for (size_t done = 0; done < total; done += block) {
    float* out[2] = {outL.data(), outR.data()};
    synth.process(nullptr, out, nullptr, int(block));
    for (size_t i = 0; i < block; ++i) {
      finite = finite && std::isfinite(outL[i]) && std::isfinite(outR[i]);
      peak = std::max({peak, std::fabs(outL[i]), std::fabs(outR[i])});
    }
  }
  INFO("peak " << peak);
  REQUIRE(finite);
  REQUIRE(peak < 8.0f);  // 4.0 before the Granular trim (+5 dB, G11) raised the loop's gain; it must stay bounded, not grow
  REQUIRE(peak > 0.05f);  // the loop was alive
}

TEST_CASE("Reported latency includes the Part FX slot, Off included") {
  const auto quality = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  for (PluginKind kind : {PluginKind::Instrument, PluginKind::Effect}) {
    for (double rate : {44100.0, 48000.0, 96000.0}) {
      for (int type : {int(PartFxEngine::Off), kGranular, kReverb}) {
        Synth synth(kind);
        synth.setParam(masterId(MasterParam::LatencyMode), double(int(quality)));
        synth.setParam(partFxTypeId(0), type);
        synth.setParam(partFxTypeId(1), type);
        synth.prepare(rate, kBlock);
        const Part& a = synth.part(0);
        const Part& b = synth.part(1);
        INFO("kind " << int(kind) << " rate " << rate << " type " << type);
        REQUIRE(synth.latencySamples() == a.latencySamples());
        REQUIRE(a.latencySamples() == b.latencySamples());
        REQUIRE(synth.latencySamples() == a.voiceLatencySamples() + a.partFx().latencySamples());
        REQUIRE(a.partFx().latencySamples() > 0);  // reported even with Part FX Off
        REQUIRE(a.partFx().latencySamples() == b.partFx().latencySamples());
      }
    }
  }
}

TEST_CASE("EG13 FX: the Input through Part FX lines up with Dry") {
  for (double rate : {44100.0, 48000.0}) {
    Synth synth(PluginKind::Effect);
    test::m3Patch(synth);
    synth.setParam(filterTypeId(0), 0);
    synth.setParam(partId(0, PartParam::Trigger), 0);  // no voice sound: only the Input through the FX
    synth.setParam(partFxTypeId(0), kReverb);
    synth.setParam(partId(0, PartParam::PartFxInput), 1);  // Input
    synth.setParam(partFxId(0, 0), 0.0);                   // Amount 0: the effect passes its input
    synth.setParam(masterId(MasterParam::Dry), 0.0);
    synth.prepare(rate, kBlock);
    const size_t latency = size_t(synth.latencySamples());
    const Stereo in = sineInput(9600, rate);
    Stereo out(9600);
    run(synth, out, 0, 9600, &in);
    // The processed Input is the Input delayed by the reported latency (what Dry carries).
    double worst = 0.0;
    // The slot's trim (ruling G11) scales the engine's whole output, a passing dry signal included.
    const float trim = partFxTrim(int(PartFxEngine::Reverb));
    for (size_t i = latency + 2400; i < 9600; ++i) {
      worst = std::max(worst, double(std::fabs(out.l[i] - trim * in.l[i - latency])));
    }
    INFO("rate " << rate << " latency " << latency << " worst error " << worst);
    REQUIRE(worst < 0.02);
  }
}

TEST_CASE("EG13 FX init patch processes Input through Granular with no MIDI") {
  auto render = [](bool granular) {
    Synth synth(PluginKind::Effect);
    if (!granular) synth.setParam(partFxTypeId(0), 0);
    synth.prepare(kRate, kBlock);
    const Stereo in = sineInput(48000, kRate);
    Stereo out(48000);
    run(synth, out, 0, 48000, &in);
    for (float x : out.l) REQUIRE(std::isfinite(x));
    return out.l;
  };
  Synth init(PluginKind::Effect);
  REQUIRE(init.engine(SlotKind::PartFx, 0) == kGranular);
  REQUIRE(init.engine(SlotKind::PartFx, 1) == int(PartFxEngine::Off));
  const auto withFx = render(true);
  const auto without = render(false);
  REQUIRE(test::rms(withFx, 24000, 48000) > 0.01);  // it sounds with no MIDI
  INFO("RMS difference from Part FX Off " << diffRms(withFx, without, 24000, 48000));
  REQUIRE(diffRms(withFx, without, 24000, 48000) > 0.01);  // and Granular is in the chain
}
