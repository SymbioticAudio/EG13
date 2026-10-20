#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "alloc_counter.h"
#include "part/part.h"
#include "test_engines.h"
#include "test_helpers.h"

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
  std::vector<float> l(kBlock), r(kBlock);
  float* out[2] = {l.data(), r.data()};
  const ParamValues p;
  part.process(out, p, kBlock);
  PartSettings s;
  test::ScopedAllocationCounter counter;
  for (int block = 0; block < 200; ++block) {
    if (block == 10) for (int key : {60, 64, 67, 71}) part.noteOn(key, 0.8f);
    if (block == 50) part.pitchBend(0.5f);
    if (block == 60) {
      s.voiceCount = 3;
      part.applySettings(s);
    }
    if (block == 100) {
      s.alwaysOn = true;
      part.applySettings(s);
    }
    part.process(out, p, kBlock);
  }
  const size_t allocations = counter.count();
  REQUIRE(allocations == 0);
}
