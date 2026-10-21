#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "alloc_counter.h"
#include "engine/engine_registry.h"
#include "engine/ripples_engine.h"
#include "engine/warps_engine.h"
#include "params/param_ids.h"
#include "synth/param_table.h"
#include "synth/synth.h"
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

// The exact pass-through tests below predate the Filter: they run it Off.
void filtersOff(Synth& synth) {
  for (int part = 0; part < kNumParts; ++part) synth.setParam(filterTypeId(part), 0);
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
  synth.setParam(partId(0, PartParam::Trigger), 1);
  synth.setParam(masterId(MasterParam::Dry), 0.0);
  synth.prepare(kRate, kBlock);
  Stereo out(9600);
  run(synth, out, 0, 9600);
  REQUIRE(synth.activeVoiceCount(0) == 1);
  REQUIRE(test::rms(out.l, 4800, 9600) > 0.1);
}

TEST_CASE("Event timing is independent of block slicing") {
  auto renderWith = [](int chunk) {
    Synth synth(PluginKind::Instrument);
    synth.prepare(44100.0, 4096);
    Stereo out(9000);
    run(synth, out, 0, 1001, nullptr, nullptr, chunk);
    synth.noteOn(0, 64, 0.9f);
    run(synth, out, 1001, 9000, nullptr, nullptr, chunk);
    return out.l;
  };
  const auto a = renderWith(1);
  REQUIRE(a == renderWith(7));
  REQUIRE(a == renderWith(4096));
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
  REQUIRE(std::string(synth.paramName(filterParamIndex(0, 0))) == "(unused)");
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
