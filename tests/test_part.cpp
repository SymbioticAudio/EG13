#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include <sys/resource.h>

#include "alloc_counter.h"
#include "engine/audio_input_engine.h"
#include "engine/engine_registry.h"
#include "engine/tides_engine.h"
#include "part/part.h"
#include "test_engines.h"
#include "test_helpers.h"
#include "test_mod_engines.h"
#include "voice/processor_factory.h"

using namespace eg13;

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

SourceSpec sine() { return {std::make_unique<test::SineEngine>(), 48000.0, 12}; }

struct Stereo {
  std::vector<float> l, r;
};

Stereo run(Part& part, int n, const ParamValues& p = ParamValues{}) {
  Stereo s{std::vector<float>(static_cast<size_t>(n)), std::vector<float>(static_cast<size_t>(n))};
  for (int done = 0; done < n;) {
    const int count = std::min(kBlock, n - done);
    float* out[2] = {s.l.data() + done, s.r.data() + done};
    part.process(out, p, count);
    done += count;
  }
  return s;
}

Part preparedSinePart() {
  Part part(sine);
  part.prepare(kRate, kBlock);
  return part;
}

SourceSpec steady() { return {std::make_unique<test::ConstantEngine>(), 0.0, 16}; }

// Renders n samples through the modulated path in host blocks of `block`.
Stereo runModulated(Part& part, int n, const PartParams& params, int block = kBlock) {
  Stereo s{std::vector<float>(size_t(n)), std::vector<float>(size_t(n))};
  for (int done = 0; done < n;) {
    const int count = std::min(block, n - done);
    float* out[2] = {s.l.data() + done, s.r.data() + done};
    part.process(VoiceInputs{}, out, params, count);
    done += count;
  }
  return s;
}

void setRoute(PartParams& params, int r, uint16_t source, uint16_t destination, float amount,
              uint16_t via = kSrcNone) {
  params.mod.routes.route[r] = {source, destination, via};
  params.mod.routes.amount[r] = amount;
}

PartSettings withMod(int slot, int engine, bool perPart = false) {
  PartSettings s;
  s.modEngine[slot] = engine;
  s.modPerPart[slot] = perPart;
  return s;
}

ParamValues engineDefaults(ModEngineKind kind) { return test::defaultParams(modEngineInfo(int(kind)).engine); }

// A part of real engines playing the Audio Input (1 ms attack, 5 ms release), fed a steady 0.5,
// rendered tick by tick through the modulated path. after(tick) runs before each tick.
template <typename Before>
std::vector<float> renderInputPart(Part& part, PartParams params, int ticks, Before before) {
  params.slots.source = test::defaultParams(AudioInputEngine::staticInfo());
  params.slots.source.p[1] = 0.0f;
  params.slots.source.p[2] = 0.0f;
  std::vector<float> dc(16, 0.5f), out(size_t(ticks) * 16), right(16);
  const float* in[2] = {dc.data(), dc.data()};
  VoiceInputs inputs;
  inputs.input = in;
  for (int t = 0; t < ticks; ++t) {
    before(t);
    float* o[2] = {out.data() + t * 16, right.data()};
    part.process(inputs, o, params, 16);
  }
  return out;
}

int ticksFor(double rate, double seconds) { return int(std::ceil(rate * seconds / 16.0)); }

}  // namespace

TEST_CASE("A chord uses one voice per key") {
  Part part = preparedSinePart();
  part.noteOn(60, 1.0f);
  part.noteOn(64, 1.0f);
  part.noteOn(67, 1.0f);
  run(part, kBlock);
  REQUIRE(part.activeVoiceCount() == 3);
}

TEST_CASE("The voice count limits polyphony") {
  Part part = preparedSinePart();
  PartSettings s;
  s.voiceCount = 2;
  part.applySettings(s);
  for (int key : {60, 64, 67}) part.noteOn(key, 1.0f);
  run(part, kBlock);
  REQUIRE(part.activeVoiceCount() == 2);
}

TEST_CASE("Released voices free up for new notes") {
  Part part = preparedSinePart();
  for (int key = 60; key < 68; ++key) part.noteOn(key, 1.0f);
  run(part, kBlock);
  for (int key = 60; key < 68; ++key) part.noteOff(key);
  run(part, 4800);
  REQUIRE(part.activeVoiceCount() == 0);
  for (int key = 70; key < 78; ++key) part.noteOn(key, 1.0f);
  run(part, kBlock);
  REQUIRE(part.activeVoiceCount() == 8);
}

TEST_CASE("Voice count change while notes are held releases them") {
  Part part = preparedSinePart();
  for (int key : {60, 64, 67}) part.noteOn(key, 1.0f);
  run(part, kBlock);
  PartSettings s;
  s.voiceCount = 4;
  part.applySettings(s);
  run(part, 4800);
  REQUIRE(part.activeVoiceCount() == 0);
}

TEST_CASE("Transpose and pitch bend move the pitch") {
  Part part = preparedSinePart();
  PartSettings s;
  s.transpose = 12;
  part.applySettings(s);
  part.noteOn(57, 1.0f);
  auto out = run(part, 48000);
  REQUIRE_THAT(test::estimateFrequency(out.l, kRate, 4800, 48000),
               Catch::Matchers::WithinRel(440.0, 0.001));

  s.transpose = 0;
  part.applySettings(s);
  part.pitchBend(1.0f);  // bend range 2
  out = run(part, 48000);
  REQUIRE_THAT(test::estimateFrequency(out.l, kRate, 4800, 48000),
               Catch::Matchers::WithinRel(220.0 * std::pow(2.0, 2.0 / 12.0), 0.001));
}

TEST_CASE("Pan follows a balance law and Mono centres the part") {
  Part part = preparedSinePart();
  PartSettings s;
  s.pan = -1.0f;
  part.applySettings(s);
  part.noteOn(60, 1.0f);
  auto out = run(part, 19200);
  REQUIRE(test::rms(out.r, 14400, 19200) < 1e-6);
  REQUIRE(test::rms(out.l, 14400, 19200) > 0.1);

  s.mono = true;
  part.applySettings(s);
  out = run(part, 19200);
  for (size_t i = 14400; i < 19200; ++i) REQUIRE(out.l[i] == out.r[i]);
  REQUIRE(test::rms(out.r, 14400, 19200) > 0.1);
}

TEST_CASE("Level zero silences the part") {
  Part part = preparedSinePart();
  part.noteOn(60, 1.0f);
  PartSettings s;
  s.level = 0.0f;
  part.applySettings(s);
  const auto out = run(part, 19200);
  for (size_t i = 14400; i < 19200; ++i) REQUIRE(out.l[i] == 0.0f);
}

TEST_CASE("An Always-on part recovers after a NaN") {
  Part part([] { return SourceSpec{std::make_unique<test::NanOnceEngine>(), 48000.0, 12}; });
  part.prepare(kRate, kBlock);
  PartSettings s;
  s.alwaysOn = true;
  part.applySettings(s);
  const auto out = run(part, 9600);
  REQUIRE(part.activeVoiceCount() == 1);
  REQUIRE(test::rms(out.l, 4800, 9600) > 0.1);
}

TEST_CASE("Always-on plays the base note with no MIDI and follows held keys") {
  Part part = preparedSinePart();
  PartSettings s;
  s.alwaysOn = true;
  s.baseNote = 69;
  part.applySettings(s);
  auto out = run(part, 48000);
  REQUIRE(part.activeVoiceCount() == 1);
  REQUIRE_THAT(test::estimateFrequency(out.l, kRate, 4800, 48000),
               Catch::Matchers::WithinRel(440.0, 0.001));

  part.noteOn(81, 1.0f);
  out = run(part, 48000);
  REQUIRE(part.activeVoiceCount() == 1);
  REQUIRE_THAT(test::estimateFrequency(out.l, kRate, 4800, 48000),
               Catch::Matchers::WithinRel(880.0, 0.001));

  part.noteOff(81);
  out = run(part, 48000);
  REQUIRE_THAT(test::estimateFrequency(out.l, kRate, 4800, 48000),
               Catch::Matchers::WithinRel(440.0, 0.001));

  s.alwaysOn = false;
  part.applySettings(s);
  run(part, 9600);
  REQUIRE(part.activeVoiceCount() == 0);
}

TEST_CASE("Channel and key filters") {
  Part part = preparedSinePart();
  REQUIRE(part.acceptsChannel(0));
  REQUIRE(part.acceptsChannel(15));
  PartSettings s;
  s.midiChannel = 2;
  s.keyLow = 72;
  s.keyHigh = 48;  // reversed bounds still mean 48..72
  part.applySettings(s);
  REQUIRE(part.acceptsChannel(1));
  REQUIRE_FALSE(part.acceptsChannel(0));
  REQUIRE(part.acceptsChannel(-1));
  REQUIRE(part.acceptsKey(48));
  REQUIRE(part.acceptsKey(72));
  REQUIRE_FALSE(part.acceptsKey(47));
  REQUIRE_FALSE(part.acceptsKey(73));
}

TEST_CASE("Sustain keeps released notes sounding until pedal up") {
  Part part = preparedSinePart();
  part.sustain(true);
  part.noteOn(60, 1.0f);
  run(part, kBlock);
  part.noteOff(60);
  run(part, 9600);
  REQUIRE(part.activeVoiceCount() == 1);
  part.sustain(false);
  run(part, 9600);
  REQUIRE(part.activeVoiceCount() == 0);
}

TEST_CASE("The default Plaits part makes sound") {
  Part part;
  part.prepare(44100.0, kBlock);
  part.noteOn(60, 1.0f);
  const auto out = run(part, 22050);
  REQUIRE(test::rms(out.l, 4410, 22050) > 0.01);
}

TEST_CASE("Part processing does not allocate") {
  Part part;
  part.prepare(44100.0, kBlock);
  std::vector<float> l(kBlock), r(kBlock), input(kBlock, 0.1f);
  float* out[2] = {l.data(), r.data()};
  const float* in[2] = {input.data(), input.data()};
  const ParamValues p;
  part.process(in, out, p, kBlock);
  PartSettings s;
  constexpr int kEngines[] = {2, 3, 4, 1, 0};
  test::ScopedAllocationCounter counter;
  for (int block = 0; block < 200; ++block) {
    if (block == 10) for (int key : {60, 64, 67, 71}) part.noteOn(key, 0.8f);
    if (block == 40) for (int key : {60, 64, 67, 71}) part.noteOff(key);
    // Engines 0 -> 2 -> 3 -> 4 -> 1 -> 0 with the notes held (from block 15), then again on
    // the release tails (from block 42).
    for (int first : {15, 42}) {
      const int step = block - first;
      if (step >= 0 && step < 25 && step % 5 == 0) {
        s.engine = kEngines[step / 5];
        part.applySettings(s);
      }
    }
    if (block == 50) part.pitchBend(0.5f);
    if (block == 60) {
      s.voiceCount = 3;
      part.applySettings(s);
    }
    if (block == 100) {
      s.alwaysOn = true;
      part.applySettings(s);
    }
    part.process(in, out, p, kBlock);
  }
  const size_t allocations = counter.count();
  REQUIRE(allocations == 0);
}

TEST_CASE("A part with every engine switches engines from its settings") {
  Part part;
  part.prepare(48000.0, kBlock);
  PartSettings s;
  s.engine = int(SourceEngine::Digital);
  part.applySettings(s);
  part.noteOn(69, 1.0f);
  const auto out = run(part, 48000, test::defaultParams(sourceEngineInfo(1)));
  REQUIRE_THAT(test::estimateFrequency(out.l, kRate, 9600, 48000),
               Catch::Matchers::WithinRel(440.0, 0.001));
}

TEST_CASE("A part passes its input to the voices") {
  Part part;
  part.prepare(48000.0, kBlock);
  PartSettings s;
  s.engine = int(SourceEngine::AudioInput);
  s.alwaysOn = true;
  part.applySettings(s);
  std::vector<float> input(19200, 0.25f), l(19200), r(19200);
  const ParamValues p = test::defaultParams(sourceEngineInfo(4));
  for (int done = 0; done < 19200; done += kBlock) {
    const float* in[2] = {input.data() + done, input.data() + done};
    float* out[2] = {l.data() + done, r.data() + done};
    part.process(in, out, p, kBlock);
  }
  REQUIRE(std::fabs(l[19199] - 0.25f) < 1e-4f);
}

TEST_CASE("A part switches every voice's Filter from its settings") {
  auto rmsWith = [](int filter) {
    Part part;
    part.prepare(kRate, kBlock);
    PartSettings s = part.settings();
    s.filterEngine = filter;
    part.applySettings(s);
    part.noteOn(60, 1.0f);
    SlotParams p;
    p.source = test::defaultParams(sourceEngineInfo(0));
    p.filter = test::defaultParams(filterEngineInfo(filter));
    p.filter.p[0] = 0.1f;  // cutoff ~40 Hz where it applies
    std::vector<float> l(24000), r(24000);
    for (int done = 0; done < 24000; done += kBlock) {
      float* out[2] = {l.data() + done, r.data() + done};
      part.process(VoiceInputs{}, out, p, std::min(kBlock, 24000 - done));
    }
    return test::rms(l, 4800, 24000);
  };
  const double open = rmsWith(int(FilterEngine::Off));
  REQUIRE(open > 0.01);
  for (int filter : {int(FilterEngine::Analog), int(FilterEngine::Classic), int(FilterEngine::StateVariable)}) {
    INFO("filter " << filter);
    REQUIRE(rmsWith(filter) < 0.3 * open);
  }
}

TEST_CASE("Memory per part with every engine preallocated", "[memory]") {
  struct rusage before {};
  getrusage(RUSAGE_SELF, &before);
  size_t constructed = 0, prepared = 0;
  std::unique_ptr<Part> part;
  {
    test::ScopedAllocationCounter counter;
    part = std::make_unique<Part>();
    constructed = counter.bytes();
  }
  {
    test::ScopedAllocationCounter counter;
    part->prepare(48000.0, 512);
    prepared = counter.bytes();
  }
  struct rusage after {};
  getrusage(RUSAGE_SELF, &after);
  // ru_maxrss is the process's high-water mark: informational only (0 if an earlier test in the
  // same process peaked higher). The assertion uses the counted bytes.
#ifdef __APPLE__
  const double rssMb = double(after.ru_maxrss - before.ru_maxrss) / 1048576.0;  // bytes
#else
  const double rssMb = double(after.ru_maxrss - before.ru_maxrss) / 1024.0;  // kilobytes
#endif
  std::printf("Part memory: %.2f MB constructed + %.2f MB prepared (48 kHz, 512); peak RSS grew %.2f MB\n",
              double(constructed) / 1048576.0, double(prepared) / 1048576.0, rssMb);
  REQUIRE(constructed + prepared < size_t(64) * 1024 * 1024);
}

TEST_CASE("An Always-on part starts with its Filter in place, with no fade in from Off") {
  // A 5 kHz input through the SVF low-pass at its lowest cutoff (20 Hz) is about -96 dB; a 10 ms
  // fade from Off would let the input through at up to full level.
  Part part;
  part.prepare(kRate, kBlock);
  PartSettings s;
  s.engine = int(SourceEngine::AudioInput);
  s.alwaysOn = true;
  s.filterEngine = int(FilterEngine::StateVariable);
  SlotParams p;
  p.source = test::defaultParams(sourceEngineInfo(int(SourceEngine::AudioInput)));
  p.filter = test::defaultParams(filterEngineInfo(int(FilterEngine::StateVariable)));
  p.filter.p[0] = 0.0f;
  std::vector<float> input(9600);
  for (size_t i = 0; i < input.size(); ++i) input[i] = 0.25f * float(std::sin(6.283185307179586 * 5000.0 * double(i) / kRate));
  auto peak = [&] {
    std::vector<float> l(input.size()), r(input.size());
    for (int done = 0; done < int(input.size()); done += kBlock) {
      const float* in[2] = {input.data() + done, input.data() + done};
      float* out[2] = {l.data() + done, r.data() + done};
      part.process(VoiceInputs{in, nullptr}, out, p, std::min(kBlock, int(input.size()) - done));
    }
    float m = 0.0f;
    for (float x : l) m = std::max(m, std::fabs(x));
    return m;
  };
  part.applySettings(s);  // after prepare()
  REQUIRE(peak() < 1e-3f);
  part.reset();  // re-applies the settings to reset voices
  REQUIRE(peak() < 1e-3f);
}

// Part::applySettings applies Always-on before the engine changes, in one call. A voice that
// has played (so its Filter slot would fade on a change) and gone idle, then gets Always-on and
// a new Filter together, starts on the new Filter with no fade in from the old one.
TEST_CASE("An idle voice given Always-on and a Filter in one call starts on that Filter") {
  Part part;
  part.prepare(kRate, kBlock);
  PartSettings s;
  s.engine = int(SourceEngine::AudioInput);
  s.voiceCount = 1;
  part.applySettings(s);
  SlotParams p;
  p.source = test::defaultParams(sourceEngineInfo(int(SourceEngine::AudioInput)));
  p.filter = test::defaultParams(filterEngineInfo(int(FilterEngine::StateVariable)));
  p.filter.p[0] = 0.0f;  // SVF low-pass at 20 Hz: a 5 kHz input is about -96 dB
  std::vector<float> input(9600);
  for (size_t i = 0; i < input.size(); ++i) input[i] = 0.25f * float(std::sin(6.283185307179586 * 5000.0 * double(i) / kRate));
  auto peak = [&](size_t from) {
    std::vector<float> l(input.size()), r(input.size());
    for (int done = 0; done < int(input.size()); done += kBlock) {
      const float* in[2] = {input.data() + done, input.data() + done};
      float* out[2] = {l.data() + done, r.data() + done};
      part.process(VoiceInputs{in, nullptr}, out, p, std::min(kBlock, int(input.size()) - done));
    }
    float m = 0.0f;
    for (size_t i = from; i < l.size(); ++i) m = std::max(m, std::fabs(l[i]));
    return m;
  };
  part.noteOn(60, 1.0f);
  REQUIRE(peak(4800) > 0.1f);  // the voice plays through Filter Off
  part.noteOff(60);
  for (int i = 0; i < 20 && part.activeVoiceCount() > 0; ++i) peak(0);  // released: it fades and frees
  REQUIRE(part.activeVoiceCount() == 0);
  s.alwaysOn = true;
  s.filterEngine = int(FilterEngine::StateVariable);
  part.applySettings(s);
  REQUIRE(peak(0) < 1e-3f);
}

namespace {

std::vector<SourceSpec> voiceAndSideShapers() {
  std::vector<SourceSpec> v;
  v.push_back(makeOff());
  v.push_back({std::make_unique<test::VoiceAndSideEngine>(), 0.0, 16});
  return v;
}

}  // namespace

TEST_CASE("The Input side signal reaches the Shaper aligned with the voice") {
  for (const auto alignment : {Voice::Alignment::Notes, Voice::Alignment::Input}) {  // EG13, EG13 FX
    for (const double rate : {44100.0, 48000.0, 96000.0}) {
      Part part(makeAllSources, makeOffOnly, voiceAndSideShapers);
      part.prepare(rate, kBlock, alignment);
      PartSettings s = part.settings();
      s.engine = int(SourceEngine::AudioInput);
      s.alwaysOn = true;
      s.shaperEngine = 1;
      part.applySettings(s);
      SlotParams p;
      p.source = test::defaultParams(sourceEngineInfo(int(SourceEngine::AudioInput)));
      p.shaper.p[0] = 2.5f / 4.0f;  // side 1: Input
      const int n = 8192;
      std::vector<float> input(size_t(n), 0.0f), l(size_t(n), 0.0f), r(size_t(n), 0.0f);
      input[1000] = 1.0f;
      for (int done = 0; done < n; done += kBlock) {
        const int count = std::min(kBlock, n - done);
        const float* in[2] = {input.data() + done, input.data() + done};
        float* out[2] = {l.data() + done, r.data() + done};
        part.process(VoiceInputs{in, nullptr}, out, p, count);
      }
      INFO("rate " << rate << (alignment == Voice::Alignment::Input ? " FX" : " instrument") << " latency "
                   << part.latencySamples());
      const int voicePeak = test::peakIndex(l);
      REQUIRE(std::fabs(l[size_t(voicePeak)]) > 0.5f);
      REQUIRE(std::fabs(r[size_t(voicePeak)]) > 0.5f);
      REQUIRE(test::peakIndex(r) == voicePeak);
      REQUIRE(voicePeak == 1000 + part.latencySamples());
    }
  }
}

TEST_CASE("Without Amp routes a modulated part follows the gate, closing 5 ms after release") {
  Part part(steady, test::testModEngines);
  part.prepare(kRate, kBlock);
  const PartParams params;
  part.noteOn(60, 1.0f);
  Stereo held = runModulated(part, 4800, params);
  REQUIRE(held.l[4799] == 0.5f);
  part.noteOff(60);
  Stereo released = runModulated(part, 480, params);
  REQUIRE(released.l[100] > 0.0f);
  REQUIRE(released.l[300] == 0.0f);
}

TEST_CASE("An Envelope routed to the Amp shapes the part's notes, scaled by velocity") {
  Part part(steady);
  part.prepare(kRate, kBlock);
  part.applySettings(withMod(0, int(ModEngineKind::Envelope)));
  PartParams params;
  params.mod.slot[0] = engineDefaults(ModEngineKind::Envelope);  // 1 ms attack, full sustain, 0.5 s release
  setRoute(params, 0, modOutputSource(0, 0), kDestAmp, 1.0f, kSrcVelocity);
  part.noteOn(60, 0.5f);
  const Stereo held = runModulated(part, 9600, params);
  REQUIRE(std::fabs(held.l[9599] - 0.5f * 0.5f) < 1e-4f);
  part.noteOff(60);
  const Stereo released = runModulated(part, 28800, params);
  REQUIRE(released.l[4800] < 0.25f);   // releasing over about half a second
  REQUIRE(released.l[4800] > 0.01f);
  REQUIRE(released.l[28799] < 1e-4f);
}

TEST_CASE("Modulation does not depend on host block slicing") {
  // Counts derived from the rate: the note starts after about 21 ms, is released at 0.25 s and
  // the render ends at 0.5 s (1001, 12000 and 24000 samples at 48 kHz).
  for (const double rate : {44100.0, 48000.0, 96000.0}) {
    const int noteAt = int(rate / 48.0) + 1;
    const int releaseAt = int(rate / 4.0);
    const int total = int(rate / 2.0);
    auto render = [&](int block) {
      Part part(sine);
      part.setSeedBase(7);
      part.prepare(rate, 512);
      PartSettings s;
      s.modEngine[0] = int(ModEngineKind::Envelope);
      s.modEngine[1] = int(ModEngineKind::Lfo);
      part.applySettings(s);
      PartParams params;
      params.timing.beatsPerSample = 2.0 / rate;
      params.mod.slot[0] = engineDefaults(ModEngineKind::Envelope);
      params.mod.slot[0].p[3] = 0.3f;  // decay
      params.mod.slot[0].p[5] = 0.4f;  // sustain
      params.mod.slot[1] = engineDefaults(ModEngineKind::Lfo);
      params.mod.slot[1].p[2] = TidesEngine::frequencyFor(6.0, TidesEngine::Medium);
      setRoute(params, 0, modOutputSource(0, 0), kDestAmp, 1.0f, kSrcVelocity);
      setRoute(params, 1, modOutputSource(1, 0), kDestPitch, 0.05f);
      setRoute(params, 2, modOutputSource(1, 1), kDestPan, 0.3f);
      Stereo out{std::vector<float>(size_t(total)), std::vector<float>(size_t(total))};
      auto process = [&](int from, int to) {
        for (int done = from; done < to;) {
          const int count = std::min(block, to - done);
          float* o[2] = {out.l.data() + done, out.r.data() + done};
          part.process(VoiceInputs{}, o, params, count);
          done += count;
        }
      };
      process(0, noteAt);
      part.noteOn(57, 0.9f);
      process(noteAt, releaseAt);
      part.noteOff(57);
      process(releaseAt, total);
      return out;
    };
    const Stereo a = render(512);
    for (int block : {1, 7, 16, 100}) {
      const Stereo b = render(block);
      INFO("rate " << rate << " block " << block);
      REQUIRE(a.l == b.l);
      REQUIRE(a.r == b.r);
    }
  }
}

TEST_CASE("A modulated Part Level holds across settings updates, whatever the host block size") {
  // Preflight ruling G5: applySettings() (once per host block) keeps the last modulated level, so
  // the output does not depend on how often the settings arrive.
  for (const double rate : {44100.0, 48000.0, 96000.0}) {
    const int total = int(rate / 4.0);
    auto render = [&](int block) {
      Part part(steady);
      part.setSeedBase(11);
      part.prepare(rate, 512);
      PartSettings s = withMod(0, int(ModEngineKind::Lfo), true);
      s.level = 0.8f;
      s.pan = 0.2f;
      part.applySettings(s);
      PartParams params;
      params.timing.beatsPerSample = 2.0 / rate;
      params.mod.slot[0] = engineDefaults(ModEngineKind::Lfo);
      params.mod.slot[0].p[2] = TidesEngine::frequencyFor(9.0, TidesEngine::Medium);
      setRoute(params, 0, modOutputSource(0, 0), kDestPartLevel, -0.5f);
      part.noteOn(60, 1.0f);
      Stereo out{std::vector<float>(size_t(total)), std::vector<float>(size_t(total))};
      for (int done = 0; done < total;) {
        const int count = std::min(block, total - done);
        float* o[2] = {out.l.data() + done, out.r.data() + done};
        part.applySettings(s);  // as the Synth does before every block
        part.process(VoiceInputs{}, o, params, count);
        done += count;
      }
      return out;
    };
    const Stereo a = render(512);
    REQUIRE(*std::min_element(a.l.begin() + total / 2, a.l.end()) < 0.5f * 0.8f * 0.8f);  // the LFO moves the level
    for (int block : {1, 7, 100}) {
      const Stereo b = render(block);
      INFO("rate " << rate << " block " << block);
      REQUIRE(a.l == b.l);
      REQUIRE(a.r == b.r);
    }
  }
}

TEST_CASE("Per-voice mod engines retrigger on each note, per-part ones on the first key") {
  Part part(sine, test::testModEngines);
  part.prepare(kRate, kBlock);
  PartSettings s;
  s.modEngine[0] = test::kTestStep;
  s.modEngine[1] = test::kTestStep;
  s.modPerPart[1] = true;
  part.applySettings(s);
  const PartParams params;
  part.noteOn(60, 1.0f);
  runModulated(part, 64, params);
  part.noteOn(64, 1.0f);
  runModulated(part, 64, params);
  part.noteOff(60);
  part.noteOff(64);
  runModulated(part, 64, params);
  part.noteOn(67, 1.0f);  // after every key was up
  runModulated(part, 64, params);
  const PartModulation& m = part.modulation();
  REQUIRE(m.outputs(0, 0)[2] == 0.1f);  // voice 0: one note
  REQUIRE(m.outputs(0, 1)[2] == 0.1f);  // voice 1: one note
  REQUIRE(std::fabs(m.outputs(1, -1)[2] - 0.2f) < 1e-6f);  // the part: first keys twice
  REQUIRE(m.outputs(1, -1)[1] == 1.0f);  // part gate high
}

TEST_CASE("Always on: the Amp stays open, per-voice mod engines follow the held keys") {
  Part part(steady, test::testModEngines);
  part.prepare(kRate, kBlock);
  PartSettings s = withMod(0, test::kTestStep);
  s.alwaysOn = true;
  part.applySettings(s);
  const PartParams params;
  Stereo out = runModulated(part, 960, params);
  REQUIRE(out.l[959] == 0.5f);                       // sounding with no MIDI
  REQUIRE(part.modulation().outputs(0, 0)[1] == 0.0f);  // no key: gate low
  part.noteOn(60, 1.0f);
  part.noteOn(64, 1.0f);
  runModulated(part, 64, params);
  REQUIRE(part.modulation().outputs(0, 0)[1] == 1.0f);
  REQUIRE(part.modulation().outputs(0, 0)[2] == 0.1f);  // one trigger for the chord
  part.noteOff(60);
  part.noteOff(64);
  out = runModulated(part, 64, params);
  REQUIRE(part.modulation().outputs(0, 0)[1] == 0.0f);
  REQUIRE(out.l[63] == 0.5f);  // the Amp is still open
}

TEST_CASE("MPE member expression reaches only its channel's voices, and earlier messages count") {
  Part part(sine, test::testModEngines);
  part.prepare(kRate, kBlock);
  PartSettings s;
  s.mpe = 1;  // lower zone: master channel 1 (0), members 2..16 (1..15)
  s.mpeMaster = 0;
  s.mpeFirst = 1;
  s.mpeLast = 15;
  part.applySettings(s);
  PartParams params;
  setRoute(params, 0, kSrcPressure, uint16_t(kDestSourceP + 0), 1.0f);
  part.memberExpression(3, Expression::Pressure, 0.7f);  // before the note on channel 4
  part.noteOn(60, 1.0f, 1);
  part.noteOn(64, 1.0f, 3);
  runModulated(part, 32, params);
  REQUIRE(part.lastControl(0).params.source.p[0] == 0.0f);
  REQUIRE(part.lastControl(1).params.source.p[0] == 0.7f);
  part.memberExpression(1, Expression::Bend, 2.0f);
  runModulated(part, 32, params);
  REQUIRE(part.lastControl(0).pitch == 2.0f);
  REQUIRE(part.lastControl(1).pitch == 0.0f);
  REQUIRE(part.acceptsChannel(0));
  REQUIRE(part.isMemberChannel(5));
  REQUIRE_FALSE(part.isMemberChannel(0));
}

TEST_CASE("Polyphonic pressure and release velocity reach their own voice") {
  Part part(sine, test::testModEngines);
  part.prepare(kRate, kBlock);
  PartParams params;
  setRoute(params, 0, kSrcPressure, uint16_t(kDestSourceP + 0), 1.0f);
  setRoute(params, 1, kSrcReleaseVelocity, uint16_t(kDestSourceP + 1), 1.0f);
  part.noteOn(60, 1.0f);
  part.noteOn(64, 1.0f);
  part.noteExpression(-1, 64, Expression::Pressure, 0.5f);
  runModulated(part, 32, params);
  REQUIRE(part.lastControl(0).params.source.p[0] == 0.0f);
  REQUIRE(part.lastControl(1).params.source.p[0] == 0.5f);
  REQUIRE(part.lastControl(0).params.source.p[1] == 0.0f);  // 0 until released
  part.noteOff(60, 0.3f);
  runModulated(part, 32, params);
  REQUIRE(part.lastControl(0).params.source.p[1] == 0.3f);
}

TEST_CASE("A route into Part Level moves the part's level") {
  Part part(steady, test::testModEngines);
  part.prepare(kRate, kBlock);
  PartParams params;
  setRoute(params, 0, kSrcVelocity, kDestPartLevel, -0.5f);
  part.noteOn(60, 1.0f);
  const Stereo out = runModulated(part, 9600, params);
  REQUIRE(std::fabs(out.l[9599] - 0.5f * 0.5f) < 1e-3f);
}

TEST_CASE("A modulated part does not allocate") {
  Part part;
  part.setSeedBase(3);
  part.prepare(kRate, kBlock);
  PartSettings s;
  for (int slot = 0; slot < kNumModSlots; ++slot) s.modEngine[slot] = 1 + slot;
  part.applySettings(s);
  PartParams params;
  for (int slot = 0; slot < kNumModSlots; ++slot) params.mod.slot[slot] = engineDefaults(ModEngineKind(1 + slot));
  setRoute(params, 0, modOutputSource(0, 0), kDestAmp, 1.0f, kSrcVelocity);
  setRoute(params, 1, modOutputSource(1, 0), kDestPitch, 0.02f, kSrcModWheel);
  setRoute(params, 2, modOutputSource(2, 3), uint16_t(kDestSourceP + 2), 0.3f);
  setRoute(params, 3, modOutputSource(3, 0), modParamDestination(1, 2), 0.2f);
  std::vector<float> l(kBlock), r(kBlock);
  float* out[2] = {l.data(), r.data()};
  part.process(VoiceInputs{}, out, params, kBlock);
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int b = 0; b < 300; ++b) {
      if (b % 40 == 0) {
        for (int k = 0; k < 4; ++k) part.noteOn(48 + 7 * k + b % 5, 0.8f, k);
      }
      if (b % 40 == 30) part.allNotesOff();
      if (b == 100) part.memberExpression(1, Expression::Bend, 1.0f);
      if (b == 150) part.setController(PartController::ModWheel, 0.7f);
      params.timing.beat += 0.1;
      part.process(VoiceInputs{}, out, params, 1 + b % kBlock);
    }
    part.reset();
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}

TEST_CASE("An envelope routed to the Amp keeps its attack at the output") {
  // Fix round 1 ruling: the routed Amp acts latencySamples() after its tick, on the sound made then.
  const double rate = GENERATE(44100.0, 48000.0, 96000.0);
  CAPTURE(rate);
  const int ticks = ticksFor(rate, 0.06);
  auto render = [&](bool envelope, std::vector<float>* amps, int* latency) {
    Part part;
    part.setSeedBase(5);
    part.prepare(rate, 256);
    PartSettings s = withMod(0, int(ModEngineKind::Envelope));
    s.engine = int(SourceEngine::AudioInput);
    part.applySettings(s);
    *latency = part.latencySamples();
    PartParams params;
    params.timing.beatsPerSample = 2.0 / rate;
    params.mod.slot[0] = engineDefaults(ModEngineKind::Envelope);
    params.mod.slot[0].p[3] = 0.2f;  // a short decay
    params.mod.slot[0].p[5] = 0.0f;  // to no sustain: a percussive shape
    if (envelope) {
      setRoute(params, 0, modOutputSource(0, 0), kDestAmp, 1.0f, kSrcVelocity);
    } else {
      setRoute(params, 0, kSrcVelocity, kDestAmp, 1.0f);  // the Amp held open: the sound itself
    }
    part.noteOn(60, 1.0f);
    std::vector<float> out = renderInputPart(part, params, ticks, [&](int t) {
      if (t > 0) amps->push_back(clamp01(part.lastControl(0).amp));
    });
    amps->push_back(clamp01(part.lastControl(0).amp));
    return out;
  };
  std::vector<float> envelope, open;
  int latency = 0;
  const std::vector<float> wet = render(true, &envelope, &latency);
  const std::vector<float> dry = render(false, &open, &latency);
  REQUIRE(latency > 400);
  // The envelope peaks within the voice latency: without the alignment its attack would be lost.
  const size_t peak = size_t(std::max_element(envelope.begin(), envelope.end()) - envelope.begin());
  REQUIRE(envelope[peak] > 0.5f);
  REQUIRE(int(peak) * 16 < latency);
  // The Amp each tick sets, moving across the tick, applied latencySamples() later.
  auto ampAt = [](const std::vector<float>& a, size_t u) {
    const size_t k = u / 16;
    const float from = k == 0 ? 0.0f : a[k - 1];
    return from + (a[k] - from) * float(u % 16 + 1) / 16.0f;
  };
  float loudest = 0.0f;
  for (size_t s = size_t(latency); s < wet.size(); ++s) {
    if (dry[s] < 1e-3f) continue;
    const size_t u = s - size_t(latency);
    const float sound = dry[s] / ampAt(open, u);
    INFO("sample " << s);
    REQUIRE(std::fabs(wet[s] - sound * ampAt(envelope, u)) < 1e-4f);
    loudest = std::max(loudest, wet[s] / sound);
  }
  REQUIRE(loudest > 0.95f * envelope[peak]);  // the attack's peak reaches the output
}

TEST_CASE("A modulated Part Level lines up with the voices' sound") {
  const double rate = GENERATE(44100.0, 48000.0, 96000.0);
  CAPTURE(rate);
  const int change = ticksFor(rate, 0.1);
  const int ticks = change + ticksFor(rate, 0.05);
  int latency = 0;
  auto render = [&](bool move) {
    Part part;
    part.prepare(rate, 256);
    PartSettings s;
    s.engine = int(SourceEngine::AudioInput);
    part.applySettings(s);
    latency = part.latencySamples();
    PartParams params;
    params.timing.beatsPerSample = 2.0 / rate;
    setRoute(params, 0, kSrcCcA, kDestPartLevel, -0.5f);
    part.noteOn(60, 1.0f);
    return renderInputPart(part, params, ticks, [&](int t) {
      if (move && t == change) part.setController(PartController::CcA, 1.0f);
    });
  };
  const std::vector<float> still = render(false);
  const std::vector<float> moved = render(true);
  const size_t edge = size_t(change) * 16 + size_t(latency);  // when the sound of that tick arrives
  REQUIRE(latency > 400);
  for (size_t s = 0; s < edge; ++s) REQUIRE(moved[s] == still[s]);
  REQUIRE(still[edge + 15] > 0.1f);
  REQUIRE(moved[edge + 15] < still[edge + 15] - 1e-4f);
}
