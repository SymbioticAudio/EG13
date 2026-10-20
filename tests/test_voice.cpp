#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

#include "alloc_counter.h"
#include "engine/plaits_engine.h"
#include "test_engines.h"
#include "test_helpers.h"
#include "voice/source_factory.h"
#include "voice/voice.h"

using namespace eg13;

namespace {

SourceSpec sine() { return {std::make_unique<test::SineEngine>(), 48000.0, 12}; }

std::vector<float> run(Voice& voice, int n, const ParamValues& p = ParamValues{}) {
  std::vector<float> left(static_cast<size_t>(n), 0.0f), right(static_cast<size_t>(n), 0.0f);
  for (int done = 0; done < n;) {
    const int count = std::min(256, n - done);
    float* out[2] = {left.data() + done, right.data() + done};
    voice.process(out, p, count);
    done += count;
  }
  return left;
}

}  // namespace

TEST_CASE("An idle voice adds nothing") {
  Voice voice(sine());
  voice.prepare(44100.0, 256);
  REQUIRE_FALSE(voice.active());
  for (float s : run(voice, 4410)) REQUIRE(s == 0.0f);
}

TEST_CASE("A triggered voice plays its key in tune") {
  Voice voice(sine());
  voice.prepare(44100.0, 256);
  voice.trigger(69, 1.0f);
  const auto out = run(voice, 44100);
  REQUIRE_THAT(test::estimateFrequency(out, 44100.0, 4410, 44100),
               Catch::Matchers::WithinRel(440.0, 0.001));
}

TEST_CASE("The pitch offset transposes the voice") {
  Voice voice(sine());
  voice.prepare(48000.0, 256);
  voice.setPitchOffset(12.0f);
  voice.trigger(57, 1.0f);
  const auto out = run(voice, 48000);
  REQUIRE_THAT(test::estimateFrequency(out, 48000.0, 4800, 48000),
               Catch::Matchers::WithinRel(440.0, 0.001));
}

TEST_CASE("Glide moves gradually to a new key while sounding") {
  Voice voice(sine());
  voice.prepare(48000.0, 256);
  voice.setGlideSeconds(0.1f);
  voice.trigger(60, 1.0f);
  REQUIRE(voice.currentNote() == 60.0f);
  run(voice, 4800);
  voice.glideTo(72);
  run(voice, 480);  // 10 ms
  REQUIRE(voice.currentNote() > 60.0f);
  REQUIRE(voice.currentNote() < 72.0f);
  run(voice, 96000);
  REQUIRE(voice.currentNote() == 72.0f);
}

TEST_CASE("Glide speed does not depend on the host block size") {
  auto glideWith = [](int block) {
    Voice voice(sine());
    voice.prepare(48000.0, 256);
    voice.setGlideSeconds(0.1f);
    voice.trigger(60, 1.0f);
    std::vector<float> l(256, 0.0f), r(256, 0.0f);
    float* out[2] = {l.data(), r.data()};
    const ParamValues p;
    voice.process(out, p, 256);
    voice.glideTo(72);
    for (int done = 0; done < 4800; done += block) voice.process(out, p, std::min(block, 4800 - done));
    return voice.currentNote();
  };
  const float reference = glideWith(16);
  REQUIRE(std::fabs(glideWith(7) - reference) < 1e-3f);
  REQUIRE(std::fabs(glideWith(1) - reference) < 1e-3f);
  REQUIRE(std::fabs(glideWith(256) - reference) < 1e-3f);
}

TEST_CASE("A voice that was idle starts at its key even with glide on") {
  Voice voice(sine());
  voice.prepare(48000.0, 256);
  voice.setGlideSeconds(1.0f);
  voice.trigger(72, 1.0f);
  REQUIRE(voice.currentNote() == 72.0f);
}

TEST_CASE("A released voice goes idle 50 ms after it falls silent") {
  Voice voice(sine());
  voice.prepare(48000.0, 256);
  voice.trigger(60, 1.0f);
  run(voice, 4800);
  voice.release();
  REQUIRE_FALSE(voice.gate());
  run(voice, 1920);  // 40 ms of silence: less than the 50 ms limit
  REQUIRE(voice.active());
  run(voice, 4800);
  REQUIRE_FALSE(voice.active());
}

TEST_CASE("A voice whose engine outputs NaN is muted and freed") {
  struct NanEngine final : VoiceEngine {
    void prepare(double, int) override {}
    void reset() override {}
    void noteOn(float, float) override {}
    void noteOff() override {}
    void setNote(float) override {}
    void process(const float* const*, float* const* out, const ParamValues&, int n) override {
      for (int i = 0; i < n; ++i) out[0][i] = out[1][i] = std::numeric_limits<float>::quiet_NaN();
    }
    const EngineInfo& info() const override {
      static const EngineInfo kInfo = {"nan", "NaN", 0, nullptr};
      return kInfo;
    }
  };
  Voice voice({std::make_unique<NanEngine>(), 48000.0, 12});
  voice.prepare(48000.0, 256);
  voice.trigger(60, 1.0f);
  for (float s : run(voice, 512)) REQUIRE(s == 0.0f);
  REQUIRE(voice.active());  // the gate is still on, so the voice retriggers
  voice.release();
  run(voice, 256);
  REQUIRE_FALSE(voice.active());
}

TEST_CASE("A voice recovers after a one-off NaN") {
  Voice voice({std::make_unique<test::NanOnceEngine>(), 48000.0, 12});
  voice.prepare(48000.0, 256);
  voice.trigger(60, 1.0f);
  for (float s : run(voice, 256)) REQUIRE(s == 0.0f);
  const auto out = run(voice, 4800);
  REQUIRE(test::rms(out, 0, out.size()) > 0.1);
  REQUIRE(voice.active());
}

TEST_CASE("Releasing right after a NaN block does not leave a stuck note") {
  Voice voice({std::make_unique<test::NanOnceEngine>(), 48000.0, 12});
  voice.prepare(48000.0, 256);
  voice.trigger(60, 1.0f);
  for (float s : run(voice, 256)) REQUIRE(s == 0.0f);  // NaN block: muted, retrigger pending
  REQUIRE(voice.active());
  voice.release();  // before the next process() call re-sends the note
  run(voice, 9600);  // 200 ms at 48 kHz: well past the 50 ms silence limit
  REQUIRE_FALSE(voice.active());
}

TEST_CASE("Idle and active voices start notes with the same delay") {
  Voice voice({std::make_unique<test::ImpulseEngine>(), PlaitsEngine::kNativeRate, 12});
  voice.prepare(48000.0, 256);
  auto peakIndex = [](const std::vector<float>& x) {
    size_t peak = 0;
    for (size_t i = 0; i < x.size(); ++i) {
      if (std::fabs(x[i]) > std::fabs(x[peak])) peak = i;
    }
    return double(peak);
  };
  voice.trigger(60, 1.0f);
  const double d1 = peakIndex(run(voice, 4000));
  REQUIRE(voice.active());
  voice.trigger(60, 1.0f);
  const double d2 = peakIndex(run(voice, 4000));
  const double tolerance = std::ceil(12.0 * 48000.0 / PlaitsEngine::kNativeRate) + 2.0;
  INFO("idle " << d1 << " active " << d2 << " latency " << voice.latencySamples());
  REQUIRE(std::fabs(d1 - d2) <= tolerance);
  REQUIRE(std::fabs(d1 - double(voice.latencySamples())) <= tolerance);
}

TEST_CASE("The default Plaits source makes sound") {
  Voice voice(makePlaitsSource());
  voice.prepare(44100.0, 256);
  voice.trigger(60, 1.0f);
  REQUIRE(test::rms(run(voice, 22050), 4410, 22050) > 0.01);
}

TEST_CASE("Voice processing does not allocate") {
  Voice voice(makePlaitsSource());
  voice.prepare(44100.0, 256);
  std::vector<float> left(256), right(256);
  float* out[2] = {left.data(), right.data()};
  const ParamValues p;
  voice.trigger(60, 1.0f);
  voice.process(out, p, 256);
  test::ScopedAllocationCounter counter;
  for (int block = 0; block < 200; ++block) {
    if (block == 20) voice.glideTo(67);
    if (block == 100) voice.release();
    if (block == 150) voice.trigger(64, 0.5f);
    voice.process(out, p, 256);
  }
  const size_t allocations = counter.count();
  REQUIRE(allocations == 0);
}
