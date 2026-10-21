#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "alloc_counter.h"
#include "engine/engine_registry.h"
#include "part/part.h"
#include "test_engines.h"
#include "test_helpers.h"
#include "voice/processor_factory.h"

#include <sys/resource.h>

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
