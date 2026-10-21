#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

#include "alloc_counter.h"
#include "engine/engine_registry.h"
#include "engine/plaits_engine.h"
#include "engine/warps_engine.h"
#include "test_engines.h"
#include "test_helpers.h"
#include "voice/processor_factory.h"
#include "voice/processor_slot.h"
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
      static const EngineInfo kInfo = {"nan", "NaN", 0, nullptr, false};
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

TEST_CASE("Switching engines does not allocate") {
  Voice voice(makeAllSources());
  voice.prepare(44100.0, 256);
  std::vector<float> left(256), right(256), input(256, 0.1f);
  float* out[2] = {left.data(), right.data()};
  const float* in[2] = {input.data(), input.data()};
  const ParamValues p;
  voice.trigger(60, 1.0f);
  voice.process(in, out, p, 256);
  test::ScopedAllocationCounter counter;
  for (int block = 0; block < 240; ++block) {
    if (block % 20 == 0) voice.setEngine((block / 20) % 5);  // held, then released
    if (block == 120) voice.release();
    voice.process(in, out, p, 256);
  }
  const size_t allocations = counter.count();
  REQUIRE(allocations == 0);
}

namespace {

std::vector<SourceSpec> impulseAndEcho() {
  std::vector<SourceSpec> sources;
  sources.push_back({std::make_unique<test::ImpulseEngine>(), 32000.0, 16});
  sources.push_back({std::make_unique<test::EchoEngine>(), 96000.0, 24});
  return sources;
}

// SineEngine scaled by the note-on velocity.
class VelocitySineEngine final : public VoiceEngine {
 public:
  void prepare(double rate, int block) override { sine_.prepare(rate, block); }
  void reset() override { sine_.reset(); }
  void noteOn(float note, float velocity) override {
    velocity_ = velocity;
    sine_.noteOn(note, velocity);
  }
  void noteOff() override { sine_.noteOff(); }
  void setNote(float note) override { sine_.setNote(note); }
  void process(const float* const* in, float* const* out, const ParamValues& p, int n) override {
    sine_.process(in, out, p, n);
    for (int i = 0; i < n; ++i) {
      out[0][i] *= velocity_;
      out[1][i] *= velocity_;
    }
  }
  const EngineInfo& info() const override { return sine_.info(); }

 private:
  test::SineEngine sine_;
  float velocity_ = 1.0f;
};

int peakOf(const std::vector<float>& x) {
  return int(std::max_element(x.begin(), x.end(), [](float a, float b) {
               return std::fabs(a) < std::fabs(b);
             }) - x.begin());
}

}  // namespace

TEST_CASE("Every engine in a voice has the same note latency") {
  Voice voice(impulseAndEcho());
  voice.prepare(44100.0, 256);
  REQUIRE(voice.engineCount() == 2);
  const int latency = voice.latencySamples();
  voice.setEngine(0);
  voice.trigger(60, 1.0f);
  std::vector<float> l(4000, 0.0f), r(4000, 0.0f);
  float* out[2] = {l.data(), r.data()};
  voice.process(nullptr, out, ParamValues{}, 4000);
  REQUIRE(std::abs(peakOf(l) - latency) <= int(std::ceil(16.0 * 44100.0 / 32000.0)) + 2);
}

TEST_CASE("Input alignment lines up input-using engines with the rest") {
  Voice voice(impulseAndEcho());
  voice.prepare(44100.0, 256, Voice::Alignment::Input);
  const int latency = voice.latencySamples();
  voice.setEngine(1);  // echo: input path
  voice.trigger(60, 1.0f);
  std::vector<float> input(6000, 0.0f), l(6000, 0.0f), r(6000, 0.0f);
  input[1000] = 1.0f;
  for (int done = 0; done < 6000; done += 256) {
    const int count = std::min(256, 6000 - done);
    const float* in[2] = {input.data() + done, input.data() + done};
    float* out[2] = {l.data() + done, r.data() + done};
    voice.process(in, out, ParamValues{}, count);
  }
  REQUIRE(std::abs(peakOf(l) - 1000 - latency) <= int(std::ceil(24.0 * 44100.0 / 96000.0)) + 2);
}

TEST_CASE("Switching engines restarts a held note on the new engine") {
  std::vector<SourceSpec> sources;
  sources.push_back({std::make_unique<VelocitySineEngine>(), 48000.0, 12});
  sources.push_back({std::make_unique<VelocitySineEngine>(), 48000.0, 12});
  Voice voice(std::move(sources));
  voice.prepare(48000.0, 256);
  voice.trigger(69, 0.5f);
  std::vector<float> l(4800, 0.0f), r(4800, 0.0f);
  float* out[2] = {l.data(), r.data()};
  voice.process(nullptr, out, ParamValues{}, 4800);
  voice.setEngine(1);
  REQUIRE(voice.engine() == 1);
  REQUIRE(voice.active());
  std::fill(l.begin(), l.end(), 0.0f);
  voice.process(nullptr, out, ParamValues{}, 4800);
  REQUIRE(test::rms(l, 480, 4800) > 0.1);
  // Same pitch and velocity as before the switch: 440 Hz, 0.5 * 0.5 amplitude.
  REQUIRE_THAT(test::estimateFrequency(l, 48000.0, 480, 4800),
               Catch::Matchers::WithinRel(440.0, 0.001));
  REQUIRE_THAT(test::rms(l, 480, 4800), Catch::Matchers::WithinRel(0.25 / std::sqrt(2.0), 0.02));
  voice.release();
  voice.setEngine(0);
  REQUIRE_FALSE(voice.active());  // not held: the switch ends the note
}

TEST_CASE("setEngine clamps to the available engines") {
  Voice voice(makePlaitsSource());
  voice.prepare(48000.0, 256);
  voice.setEngine(3);
  REQUIRE(voice.engine() == 0);
  voice.setEngine(-1);
  REQUIRE(voice.engine() == 0);
  Voice two(impulseAndEcho());
  two.prepare(48000.0, 256);
  two.setEngine(-5);
  REQUIRE(two.engine() == 0);
  two.setEngine(7);
  REQUIRE(two.engine() == 1);
}

TEST_CASE("A voice with every engine plays each of them") {
  Voice voice(makeAllSources());
  voice.prepare(44100.0, 256);
  std::vector<float> input(22050, 0.0f);
  for (size_t i = 0; i < 400; ++i) input[i] = (i % 2) ? 0.5f : -0.5f;
  for (int e = 0; e < kNumSourceEngines; ++e) {
    INFO("engine " << e);
    voice.reset();
    voice.setEngine(e);
    ParamValues p = test::defaultParams(sourceEngineInfo(e));
    voice.trigger(60, 1.0f);
    std::vector<float> l(22050, 0.0f), r(22050, 0.0f);
    for (int done = 0; done < 22050; done += 256) {
      const int count = std::min(256, 22050 - done);
      const float* in[2] = {input.data() + done, input.data() + done};
      float* out[2] = {l.data() + done, r.data() + done};
      voice.process(in, out, p, count);
    }
    REQUIRE(test::rms(l, 0, 22050) > 1e-4);
    voice.release();
  }
}

namespace {

std::vector<SourceSpec> specs(SourceSpec a) {
  std::vector<SourceSpec> v;
  v.push_back(std::move(a));
  return v;
}

std::vector<SourceSpec> specs(SourceSpec a, SourceSpec b) {
  std::vector<SourceSpec> v;
  v.push_back(std::move(a));
  v.push_back(std::move(b));
  return v;
}

SourceSpec hostRate(std::unique_ptr<VoiceEngine> e) { return {std::move(e), 0.0, 16}; }

std::vector<float> runChain(Voice& voice, int n, const SlotParams& p, const VoiceInputs& in = VoiceInputs{},
                            std::vector<float>* right = nullptr) {
  std::vector<float> l(size_t(n), 0.0f), r(size_t(n), 0.0f);
  for (int done = 0; done < n; done += 64) {
    const int count = std::min(64, n - done);
    float* out[2] = {l.data() + done, r.data() + done};
    const float* input[2] = {in.input ? in.input[0] + done : nullptr, in.input ? in.input[1] + done : nullptr};
    const float* other[2] = {in.other ? in.other[0] + done : nullptr, in.other ? in.other[1] + done : nullptr};
    voice.process(VoiceInputs{in.input ? input : nullptr, in.other ? other : nullptr}, out, p, count);
  }
  if (right) *right = r;
  return l;
}

}  // namespace

TEST_CASE("The voice chain runs Source, then Filter, then Shaper") {
  Voice plain(sine());
  Voice chain(specs(sine()), specs(makeOff(), hostRate(std::make_unique<test::GainEngine>())),
              specs(makeOff(), hostRate(std::make_unique<test::GainEngine>())));
  for (Voice* v : {&plain, &chain}) v->prepare(48000.0, 256);
  chain.setFilterEngine(1);
  chain.setShaperEngine(1);
  plain.trigger(69, 1.0f);
  chain.trigger(69, 1.0f);
  SlotParams p;
  p.filter.p[0] = 0.5f;   // x2
  p.shaper.p[0] = 0.75f;  // x3
  const auto a = runChain(plain, 4800, SlotParams{});
  const auto b = runChain(chain, 4800, p);
  for (size_t i = 0; i < a.size(); ++i) REQUIRE(std::fabs(b[i] - 6.0f * a[i]) <= 1e-6f);
}

TEST_CASE("Voice latency adds the Filter and Shaper slots") {
  Voice voice(makeAllSources(), makeAllFilters(), makeAllShapers());
  voice.prepare(48000.0, 256);
  Voice sourceOnly(makeAllSources());
  sourceOnly.prepare(48000.0, 256);
  ProcessorSlot shapers(makeAllShapers());
  shapers.prepare(48000.0, Voice::kControlBlock);
  INFO("voice " << voice.latencySamples() << " source " << sourceOnly.latencySamples() << " shaper "
                << shapers.latencySamples());
  REQUIRE(shapers.latencySamples() > 0);
  REQUIRE(voice.latencySamples() == sourceOnly.latencySamples() + shapers.latencySamples());
}

TEST_CASE("The Shaper's side inputs follow their selectors") {
  Voice voice(specs(hostRate(std::make_unique<test::AuxSourceEngine>())), makeOffOnly(),
              specs(makeOff(), hostRate(std::make_unique<test::SideEchoEngine>())));
  voice.prepare(48000.0, 256);
  voice.setShaperEngine(1);
  voice.trigger(60, 1.0f);
  const std::vector<float> inL(256, 0.1f), inR(256, 0.3f), otherL(256, -0.4f), otherR(256, 0.0f);
  const float* input[2] = {inL.data(), inR.data()};
  const float* other[2] = {otherL.data(), otherR.data()};
  const VoiceInputs in{input, other};
  const struct {
    float selector;
    float expected;
  } cases[] = {{0.5f / 4.0f, 0.0f}, {1.5f / 4.0f, 0.75f}, {2.5f / 4.0f, 0.2f}, {3.5f / 4.0f, -0.2f}};
  for (const auto& c : cases) {
    for (int input = 0; input < 2; ++input) {
      SlotParams p;
      p.shaper.p[input] = c.selector;    // SideEchoEngine: P1 picks side 1 (out L), P2 side 2 (out R)
      p.shaper.p[1 - input] = 0.0f;      // the other one: Voice, so nothing is passed
      std::vector<float> right;
      const auto l = runChain(voice, 256, p, in, &right);
      const float got = input == 0 ? l.back() : right.back();
      const float other = input == 0 ? right.back() : l.back();
      INFO("side " << input << " selector " << c.selector);
      REQUIRE(std::fabs(got - c.expected) < 1e-6f);
      REQUIRE(other == 0.0f);
    }
  }
}

TEST_CASE("Source AUX falls back to the main output for sources without AUX") {
  Voice voice(specs(sine()), makeOffOnly(), specs(makeOff(), hostRate(std::make_unique<test::SideEchoEngine>())));
  Voice plain(sine());
  for (Voice* v : {&voice, &plain}) v->prepare(48000.0, 256);
  voice.setShaperEngine(1);
  voice.trigger(69, 1.0f);
  plain.trigger(69, 1.0f);
  SlotParams p;
  p.shaper.p[0] = 1.5f / 4.0f;  // side 1: Source AUX
  const auto l = runChain(voice, 4800, p);
  const auto sine = runChain(plain, 4800, SlotParams{});
  for (size_t i = 0; i < l.size(); ++i) REQUIRE(l[i] == sine[i]);  // (L + R) / 2 of a mono sine = L
}

TEST_CASE("Absent modulator sources give silence, not garbage") {
  Voice voice(specs(sine()), makeOffOnly(), makeAllShapers());
  voice.prepare(48000.0, 256);
  voice.setShaperEngine(int(ShaperEngine::RingMod));
  voice.trigger(69, 1.0f);
  SlotParams p;
  p.shaper = test::defaultParams(WarpsEngine::staticInfo());
  p.shaper.p[0] = 0.375f;  // digital ring modulation: silent when either input is
  p.shaper.p[4] = WarpsEngine::optionValue(1);  // Carrier: Voice
  for (int modulator : {2, 3}) {  // Modulator Input, Other part: nothing there
    p.shaper.p[5] = WarpsEngine::optionValue(modulator);
    const auto out = runChain(voice, 9600, p);
    for (float s : out) REQUIRE(std::isfinite(s));
    REQUIRE(test::rms(out, 4800, 9600) < 1e-4);
  }
  p.shaper.p[5] = WarpsEngine::optionValue(0);  // Modulator: Voice
  for (int carrier : {3, 4}) {  // Carrier Input, Other part: nothing there
    p.shaper.p[4] = WarpsEngine::optionValue(carrier);
    const auto out = runChain(voice, 9600, p);
    for (float s : out) REQUIRE(std::isfinite(s));
    REQUIRE(test::rms(out, 4800, 9600) < 1e-4);
  }
  p.shaper.p[4] = WarpsEngine::optionValue(1);  // Carrier: Voice
  p.shaper.p[5] = WarpsEngine::optionValue(1);  // Modulator: Source AUX, the sine itself
  const auto self = runChain(voice, 9600, p);
  REQUIRE(test::rms(self, 4800, 9600) > 0.01);
}

TEST_CASE("A Shaper that keeps sounding does not hold the voice forever") {
  Voice voice(specs(sine()), makeOffOnly(), specs(makeOff(), hostRate(std::make_unique<test::ConstantEngine>())));
  voice.prepare(48000.0, 256);
  voice.setShaperEngine(1);
  voice.trigger(60, 1.0f);
  runChain(voice, 4800, SlotParams{});
  voice.release();
  runChain(voice, 1920, SlotParams{});  // 40 ms: still active
  REQUIRE(voice.active());
  runChain(voice, 4800, SlotParams{});
  REQUIRE_FALSE(voice.active());
}

TEST_CASE("A NaN from the Filter mutes the block and the voice recovers") {
  Voice voice(specs(sine()), specs(makeOff(), hostRate(std::make_unique<test::NanOnceEngine>())), makeOffOnly());
  voice.prepare(48000.0, 256);
  voice.setFilterEngine(1);
  voice.trigger(60, 1.0f);
  const auto first = runChain(voice, 64, SlotParams{});
  for (size_t i = 0; i < 16; ++i) REQUIRE(first[i] == 0.0f);
  const auto later = runChain(voice, 4800, SlotParams{});
  for (float s : later) REQUIRE(std::isfinite(s));
  REQUIRE(voice.active());
}

TEST_CASE("A NaN from one slot resets only that slot") {
  // The Filter blows up on its 11th control block (samples 160..175) and stays NaN until reset.
  Voice voice(specs(sine()), specs(makeOff(), hostRate(std::make_unique<test::NanUntilResetEngine>(10))),
              makeOffOnly());
  Voice plain(sine());
  for (Voice* v : {&voice, &plain}) v->prepare(48000.0, 256);
  voice.setFilterEngine(1);
  voice.trigger(69, 1.0f);
  plain.trigger(69, 1.0f);
  const auto got = runChain(voice, 4800, SlotParams{});
  const auto expected = runChain(plain, 4800, SlotParams{});
  for (size_t i = 0; i < 160; ++i) REQUIRE(got[i] == expected[i]);
  for (size_t i = 160; i < 192; ++i) REQUIRE(got[i] == 0.0f);  // the rest of that call is muted
  // The Source was not reset: its sine carries on through the restarted Filter where it
  // stopped, after the failing control block (the muted rest of the call is not rendered).
  for (size_t i = 192; i < got.size(); ++i) REQUIRE(got[i] == expected[i - 16]);
  REQUIRE(voice.active());
}

TEST_CASE("A Filter NaN that the Shaper hides still restarts the Filter") {
  // Ring Mod reads a NaN input as 0, so its output stays finite while the Filter is blown up.
  Voice voice(specs(sine()), specs(makeOff(), hostRate(std::make_unique<test::NanUntilResetEngine>(10))),
              makeAllShapers());
  voice.prepare(48000.0, 256);
  voice.setFilterEngine(1);
  voice.setShaperEngine(int(ShaperEngine::RingMod));
  voice.trigger(69, 1.0f);
  SlotParams p;
  p.shaper = test::defaultParams(WarpsEngine::staticInfo());
  p.shaper.p[0] = 0.0f;                         // crossfade ...
  p.shaper.p[1] = 0.0f;                         // ... all the way to the carrier
  p.shaper.p[4] = WarpsEngine::optionValue(1);  // Carrier: Voice
  const auto out = runChain(voice, 9600, p);
  for (float s : out) REQUIRE(std::isfinite(s));
  REQUIRE(test::rms(out, 4800, 9600) > 0.01);
}

TEST_CASE("The Ring Mod oscillator follows the voice's note") {
  Voice voice(specs(sine()), makeOffOnly(), makeAllShapers());
  voice.prepare(48000.0, 256);
  voice.setShaperEngine(int(ShaperEngine::RingMod));
  voice.trigger(57, 1.0f);
  SlotParams p;
  p.shaper = test::defaultParams(WarpsEngine::staticInfo());  // Carrier Internal Osc
  p.shaper.p[0] = 0.0f;    // crossfade ...
  p.shaper.p[1] = 0.0f;    // ... all the way to the carrier
  p.shaper.p[6] = 0.75f;   // Osc Pitch +12 st: 440 Hz
  const auto out = runChain(voice, 48000, p);
  REQUIRE_THAT(test::estimateFrequency(out, 48000.0, 9600, 48000), Catch::Matchers::WithinRel(440.0, 0.003));
}

TEST_CASE("Voice processing with every engine and slot switches does not allocate") {
  Voice voice(makeAllSources(), makeAllFilters(), makeAllShapers());
  voice.prepare(48000.0, 256);
  std::vector<float> l(256), r(256), in(256, 0.1f);
  float* out[2] = {l.data(), r.data()};
  const float* input[2] = {in.data(), in.data()};
  SlotParams p;
  for (float& v : p.source.p) v = 0.5f;
  for (float& v : p.filter.p) v = 0.5f;
  for (float& v : p.shaper.p) v = 0.5f;
  voice.trigger(60, 1.0f);
  voice.process(VoiceInputs{input, input}, out, p, 256);
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int block = 0; block < 60; ++block) {
      if (block % 10 == 0) {
        voice.setFilterEngine((block / 10) % kNumFilterEngines);
        voice.setShaperEngine((block / 10) % kNumShaperEngines);
        voice.setEngine((block / 10) % kNumSourceEngines);
      }
      if (block == 30) voice.trigger(64, 0.7f);
      voice.process(VoiceInputs{input, input}, out, p, 256);
    }
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}
