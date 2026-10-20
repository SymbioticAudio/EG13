#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

#include "alloc_counter.h"
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
  REQUIRE(fx.param(masterId(MasterParam::Dry)) == 1.0);
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

TEST_CASE("A part set to Other part is heard on main until M2") {
  Synth synth(PluginKind::Instrument, sine);
  synth.setParam(partId(0, PartParam::Output), 2);
  synth.prepare(kRate, kBlock);
  synth.noteOn(0, 60, 1.0f);
  Stereo main(4800), bus2(4800);
  run(synth, main, 0, 4800, nullptr, &bus2);
  REQUIRE(test::rms(main.l, 480, 4800) > 0.1);
  REQUIRE(test::rms(bus2.l, 0, 4800) == 0.0);
}

TEST_CASE("FX passes its input through, delayed by the reported latency") {
  for (double rate : {44100.0, 48000.0}) {
    Synth synth(PluginKind::Effect);
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
  synth.prepare(44100.0, kBlock);
  const Stereo in = sineInput(4410, 44100.0);
  const Stereo silent(4410);
  Stereo out(3 * 4410);
  run(synth, out, 0, 4410, &in);
  run(synth, out, 4410, 8820);
  run(synth, out, 8820, 13230, &silent);
  for (size_t i = 8820; i < 13230; ++i) REQUIRE(std::fabs(out.l[i]) < 1e-6f);
}

TEST_CASE("Single mode mutes Part B audio") {
  Synth synth(PluginKind::Effect, sine);
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
