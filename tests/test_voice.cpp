#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

#include "alloc_counter.h"
#include "engine/audio_input_engine.h"
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

SourceSpec constant() { return {std::make_unique<test::ConstantEngine>(), 0.0, 16}; }

struct Rendered {
  std::vector<float> l, r;
};

// Renders `ticks` control ticks of 16 samples through the M4 path with the same controls.
Rendered renderTicks(Voice& voice, const VoiceControl& c, int ticks) {
  Rendered out{std::vector<float>(size_t(ticks) * 16, 0.0f), std::vector<float>(size_t(ticks) * 16, 0.0f)};
  for (int t = 0; t < ticks; ++t) {
    voice.beginTick(c, 16);
    float* o[2] = {out.l.data() + t * 16, out.r.data() + t * 16};
    voice.render(VoiceInputs{}, o, 16);
  }
  return out;
}

// A voice of real engines (every Source and Shaper, so the real latency) playing the Audio Input,
// fed a steady 0.5. Its Filter slot holds Off and a test gain (out = in x 4 x P1, unity at 0.25)
// that shows when the Filter's parameters change.
class InputVoice {
 public:
  explicit InputVoice(double rate) : voice(makeAllSources(), gainFilters(), makeAllShapers()) {
    voice.prepare(rate, 256);
    voice.setEngine(int(SourceEngine::AudioInput));
    params.source = test::defaultParams(AudioInputEngine::staticInfo());
    params.source.p[1] = 0.0f;  // 1 ms attack
    params.source.p[2] = 0.0f;  // 5 ms release
  }

  // `ticks` ticks of 16 samples from tick `first` on: the M4 path with control(tick) when m4,
  // else the M1-M3 path (no Amp) with the same parameters. Releases at tick `release`.
  template <typename Control>
  Rendered render(int ticks, Control control, bool m4, int release = -1) {
    Rendered out{std::vector<float>(size_t(ticks) * 16, 0.0f), std::vector<float>(size_t(ticks) * 16, 0.0f)};
    const float* in[2] = {dc_.data(), dc_.data()};
    VoiceInputs inputs;
    inputs.input = in;
    for (int t = 0; t < ticks; ++t) {
      if (t == release) voice.release();
      VoiceControl c = control(t);
      c.params.source = params.source;
      float* o[2] = {out.l.data() + t * 16, out.r.data() + t * 16};
      if (m4) {
        voice.beginTick(c, 16);
        voice.render(inputs, o, 16);
      } else {
        voice.process(inputs, o, c.params, 16);
      }
    }
    return out;
  }

  Voice voice;
  SlotParams params;

 private:
  static std::vector<SourceSpec> gainFilters() {
    std::vector<SourceSpec> filters = makeOffOnly();
    filters.push_back({std::make_unique<test::GainEngine>(), 0.0, 16});
    return filters;
  }
  std::vector<float> dc_ = std::vector<float>(16, 0.5f);
};

int ticksFor(double rate, double seconds) { return int(std::ceil(rate * seconds / 16.0)); }

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
    const float* inputSide = in.inputSide ? in.inputSide + done : nullptr;
    voice.process(VoiceInputs{in.input ? input : nullptr, in.other ? other : nullptr, inputSide}, out, p, count);
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
  const std::vector<float> inputSide(256, 0.2f);  // the Input as Part delivers it: mono, aligned
  const float* other[2] = {otherL.data(), otherR.data()};
  const VoiceInputs in{input, other, inputSide.data()};
  const struct {
    float selector;
    float expected;
  } cases[] = {{0.5f / 4.0f, 0.0f}, {1.5f / 4.0f, 0.75f}, {2.5f / 4.0f, 0.2f}, {3.5f / 4.0f, -0.2f}};
  // Other part: (L + R) / 2 = -0.2.
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
      voice.process(VoiceInputs{input, input, in.data()}, out, p, 256);
    }
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}

TEST_CASE("Without an Amp route the Amp follows the gate with 5 ms ramps") {
  // Pre-flight ruling G10: at 44.1, 48 and 96 kHz, with the ramp's length derived from the rate.
  const double rate = GENERATE(44100.0, 48000.0, 96000.0);
  CAPTURE(rate);
  const double rampSamples = rate * Voice::kAmpRampSeconds;    // 220.5, 240, 480
  const int ramp = int(std::ceil(rampSamples));                 // samples until fully open
  const int ticks = int(std::ceil(2.0 * rampSamples / 16.0));   // 10 ms of ticks
  const size_t last = size_t(ticks) * 16 - 1;
  Voice voice(constant());
  voice.prepare(rate, 256);
  voice.trigger(60, 1.0f);
  const VoiceControl c;  // not routed, centre, unity gains
  const Rendered on = renderTicks(voice, c, ticks);
  for (int i = 0; i < ramp; ++i) {
    const float level = float(std::min(1.0, double(i + 1) / rampSamples));
    REQUIRE(std::fabs(on.l[size_t(i)] - 0.5f * level) < 1e-5f);
  }
  REQUIRE(on.l[last] == 0.5f);
  REQUIRE(voice.ampLevel() == 1.0f);
  voice.release();
  const Rendered off = renderTicks(voice, c, ticks);
  for (int i = 0; i < ramp; ++i) {
    const float level = float(std::max(0.0, 1.0 - double(i + 1) / rampSamples));
    REQUIRE(std::fabs(off.l[size_t(i)] - 0.5f * level) < 1e-5f);
  }
  REQUIRE(off.l[last] == 0.0f);
}

TEST_CASE("A routed Amp follows its level, moving across each tick") {
  Voice voice(constant());
  voice.prepare(48000.0, 256);
  voice.trigger(60, 1.0f);
  VoiceControl c;
  c.ampRouted = true;
  c.amp = 0.5f;
  Rendered out = renderTicks(voice, c, 1);
  for (int i = 0; i < 16; ++i) REQUIRE(std::fabs(out.l[size_t(i)] - 0.5f * 0.5f * float(i + 1) / 16.0f) < 1e-6f);
  c.amp = 1.5f;  // clamped
  out = renderTicks(voice, c, 1);
  REQUIRE(out.l[15] == 0.5f);
  c.amp = 0.0f;  // routed at zero: silent even with the gate held
  out = renderTicks(voice, c, 2);
  REQUIRE(out.l[31] == 0.0f);
}

TEST_CASE("Pan, Voice Gain and Source gain shape the voice") {
  Voice voice(constant());
  voice.prepare(48000.0, 256);
  voice.trigger(60, 1.0f);
  VoiceControl c;
  c.ampRouted = true;
  c.amp = 1.0f;
  c.pan = 1.0f;          // right
  c.gain = 0.25f;        // -12 dB
  c.sourceGain = 2.0f;   // before the (Off) Filter
  const Rendered out = renderTicks(voice, c, 4);
  // First tick: pan and gains snap (a note's first tick from silence); the Amp opens across it.
  REQUIRE(out.l[15] == 0.0f);
  REQUIRE(std::fabs(out.r[15] - 0.5f * 2.0f * 0.25f) < 1e-6f);
  REQUIRE(std::fabs(out.r[63] - 0.25f) < 1e-6f);
  REQUIRE(out.l[63] == 0.0f);
}

TEST_CASE("Pan and gains glide across a tick after the first") {
  Voice voice(constant());
  voice.prepare(48000.0, 256);
  voice.trigger(60, 1.0f);
  VoiceControl c;
  c.ampRouted = true;
  c.amp = 1.0f;
  renderTicks(voice, c, 2);
  c.gain = 0.5f;
  const Rendered out = renderTicks(voice, c, 1);
  for (int i = 0; i < 16; ++i) {
    const float gain = 1.0f + (0.5f - 1.0f) * float(i + 1) / 16.0f;
    REQUIRE(std::fabs(out.l[size_t(i)] - 0.5f * gain) < 1e-6f);
  }
}

TEST_CASE("The pitch control transposes the voice") {
  Voice voice(sine());
  voice.prepare(48000.0, 256);
  voice.trigger(57, 1.0f);  // A3, 220 Hz
  VoiceControl c;
  c.pitch = 12.0f;
  const Rendered out = renderTicks(voice, c, 3000);
  REQUIRE(std::fabs(test::estimateFrequency(out.l, 48000.0, 4800, out.l.size()) - 440.0) < 0.5);
}

TEST_CASE("A voice is freed 50 ms after its Amp closes, even if its Source keeps sounding") {
  const double rate = GENERATE(44100.0, 48000.0, 96000.0);
  CAPTURE(rate);
  Voice voice(constant());
  voice.prepare(rate, 256);
  voice.trigger(60, 1.0f);
  const VoiceControl c;
  renderTicks(voice, c, ticksFor(rate, 0.01));  // the Amp fully open
  voice.release();
  int ticks = 0;
  while (voice.active() && ticks < 1000) {
    renderTicks(voice, c, 1);
    ++ticks;
  }
  // The release ramp, then 50 ms below -90 dBFS from the first silent tick: 152, 165 and 330
  // ticks at 44.1, 48 and 96 kHz.
  const double rampSamples = rate * Voice::kAmpRampSeconds;
  int firstSilent = 0;  // the first sample of the release below -90 dBFS
  while (0.5 * std::max(0.0, 1.0 - double(firstSilent + 1) / rampSamples) >= 3.1623e-5) ++firstSilent;
  const int expected = (firstSilent + 15) / 16 + (int(rate * 0.05) + 15) / 16;
  REQUIRE(ticks >= expected);
  REQUIRE(ticks <= expected + 1);
}

TEST_CASE("A voice's Amp starts closed on each note from silence") {
  const double rate = GENERATE(44100.0, 48000.0, 96000.0);
  CAPTURE(rate);
  Voice voice(constant());
  voice.prepare(rate, 256);
  voice.trigger(60, 1.0f);
  VoiceControl c;
  c.pan = -1.0f;
  renderTicks(voice, c, 400);
  voice.reset();
  voice.trigger(62, 1.0f);
  c.pan = 1.0f;
  const Rendered out = renderTicks(voice, c, 1);
  REQUIRE(out.l[0] == 0.0f);  // pan snapped right, no glide from the left
  REQUIRE(std::fabs(out.r[0] - float(0.5 / (rate * Voice::kAmpRampSeconds))) < 1e-6f);  // the Amp opens from 0
}

TEST_CASE("Rendering the M4 path does not allocate") {
  Voice voice(sine());
  voice.prepare(48000.0, 256);
  VoiceControl c;
  std::vector<float> l(16), r(16);
  float* o[2] = {l.data(), r.data()};
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int t = 0; t < 3000; ++t) {
      if (t % 500 == 0) voice.trigger(48 + t / 500, 0.8f);
      if (t % 500 == 400) voice.release();
      c.ampRouted = (t / 250) % 2 == 0;
      c.amp = 0.7f;
      c.pan = float(t % 7) / 3.0f - 1.0f;
      c.pitch = float(t % 5);
      voice.beginTick(c, 16);
      voice.render(VoiceInputs{}, o, 7);
      voice.render(VoiceInputs{}, o, 9);
    }
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}

TEST_CASE("A retrigger during the release ramp carries the Amp on from where it is") {
  Voice voice(constant());
  voice.prepare(48000.0, 256);
  voice.trigger(60, 1.0f);
  const VoiceControl c;
  renderTicks(voice, c, 30);
  voice.release();
  const Rendered closing = renderTicks(voice, c, 8);  // 128 of the 240 release samples
  voice.trigger(62, 1.0f);  // still sounding: not from silence
  const Rendered reopening = renderTicks(voice, c, 1);
  const float step = 0.5f / 240.0f;
  REQUIRE(closing.l[127] > 0.2f);
  REQUIRE(std::fabs(reopening.l[0] - (closing.l[127] + step)) < 1e-5f);
}

TEST_CASE("Switching the Amp between routed and the gate does not jump") {
  Voice voice(constant());
  voice.prepare(48000.0, 256);
  voice.trigger(60, 1.0f);
  VoiceControl c;
  Rendered out = renderTicks(voice, c, 30);  // gate-following, open
  const float open = out.l[479];
  c.ampRouted = true;
  c.amp = 0.5f;
  out = renderTicks(voice, c, 2);  // across one tick from 1 to 0.5
  REQUIRE(std::fabs(out.l[0] - open) <= 0.5f * 0.5f / 16.0f + 1e-6f);
  REQUIRE(std::fabs(out.l[31] - 0.25f) < 1e-6f);
  c.ampRouted = false;  // back to the gate: up from 0.5 at the 5 ms slope
  out = renderTicks(voice, c, 1);
  REQUIRE(std::fabs(out.l[0] - (0.25f + 0.5f / 240.0f)) < 1e-6f);
}

TEST_CASE("A routed Amp held open keeps a released voice sounding") {
  Voice voice(constant());
  voice.prepare(48000.0, 256);
  voice.trigger(60, 1.0f);
  VoiceControl c;
  c.ampRouted = true;
  c.amp = 1.0f;
  renderTicks(voice, c, 10);
  voice.release();
  const Rendered out = renderTicks(voice, c, 400);  // 133 ms
  REQUIRE(voice.active());
  REQUIRE(out.l[6399] == 0.5f);
}

TEST_CASE("On the M4 path a silent Shaper input still frees the voice with the Amp open") {
  // The Shaper is a constant (its output never dies, like the Ring Mod's oscillator); the Source
  // (a sine) stops at the release.
  std::vector<SourceSpec> shapers = makeOffOnly();
  shapers.push_back(constant());
  std::vector<SourceSpec> sources;
  sources.push_back(sine());
  Voice voice(std::move(sources), makeOffOnly(), std::move(shapers));
  voice.prepare(48000.0, 256);
  voice.setShaperEngine(1);
  voice.trigger(60, 1.0f);
  VoiceControl c;
  c.ampRouted = true;
  c.amp = 1.0f;
  renderTicks(voice, c, 10);
  voice.release();
  int ticks = 0;
  while (voice.active() && ticks < 1000) {
    renderTicks(voice, c, 1);
    ++ticks;
  }
  REQUIRE(ticks >= 150);  // 50 ms (plus the Shaper latency), not held forever
  REQUIRE(ticks <= 152 + voice.latencySamples() / 16);
}

TEST_CASE("The gate-following Amp lines up with the voice's delayed sound") {
  // Fix round 1 ruling: the Amp acts latencySamples() after the gate, when the note's sound
  // arrives. Real engines (their latency); counts derived from the rate.
  const double rate = GENERATE(44100.0, 48000.0, 96000.0);
  CAPTURE(rate);
  const double rampSamples = rate * Voice::kAmpRampSeconds;
  const int release = ticksFor(rate, 0.1);
  const int ticks = release + ticksFor(rate, 0.1);
  auto none = [](int) { return VoiceControl{}; };
  InputVoice reference(rate);  // the M1-M3 path: the sound without the Amp
  reference.voice.trigger(60, 1.0f);
  const Rendered dry = reference.render(ticks, none, false, release);
  InputVoice probe(rate);
  probe.voice.trigger(60, 1.0f);
  const Rendered wet = probe.render(ticks, none, true, release);

  const int latency = probe.voice.latencySamples();
  REQUIRE(latency > 400);  // the real voice latency (the Ring Mod / Vocoder's and the Modal's)
  size_t onset = 0;
  while (onset < dry.l.size() && dry.l[onset] == 0.0f) ++onset;
  REQUIRE(std::abs(int(onset) - latency) <= 16);  // the note's audible onset
  const size_t end = size_t(release) * 16 + size_t(latency);  // the audible note end
  // Opening: within one tick of the onset the Amp is at most a tick into its ramp.
  REQUIRE(wet.l[onset + 16] <= dry.l[onset + 16] * float(33.0 / rampSamples));
  REQUIRE(wet.l[onset + 16] > 0.0f);
  for (size_t s = onset; s < dry.l.size(); ++s) {
    if (dry.l[s] < 1e-3f) continue;
    const double u = double(s) - double(latency) + 1.0;  // samples of gate ramp applied
    const double amp = s < end ? std::min(1.0, u / rampSamples)
                               : std::max(0.0, 1.0 - (double(s) - double(end) + 1.0) / rampSamples);
    INFO("sample " << s);
    REQUIRE(std::fabs(double(wet.l[s]) / double(dry.l[s]) - amp) < 1e-3);
  }
  // Closing about 5 ms after the audible end, while the dry note is still sounding.
  const size_t closed = end + size_t(std::ceil(rampSamples));
  REQUIRE(dry.l[closed] > 0.05f);
  REQUIRE(wet.l[closed] == 0.0f);
  REQUIRE(wet.l[end - 1] == dry.l[end - 1]);
}

TEST_CASE("A 3 ms gate is audible") {
  const double rate = GENERATE(44100.0, 48000.0, 96000.0);
  CAPTURE(rate);
  InputVoice probe(rate);
  probe.voice.trigger(60, 1.0f);
  const Rendered out = probe.render(ticksFor(rate, 0.04), [](int) { return VoiceControl{}; }, true,
                                    ticksFor(rate, 0.003));
  // The Amp opens 3/5 of the way while the input's 1 ms attack nearly completes: about 0.28.
  REQUIRE(*std::max_element(out.l.begin(), out.l.end()) > 0.2f);
}

TEST_CASE("Filter parameters change in line with the Source's delayed sound") {
  // An envelope-like control: the Filter's gain is unity for the note's first 2 ms of control
  // ticks, then 0. The Filter must apply it to the Source's sound of those 2 ms, which arrives a
  // Source latency later.
  const double rate = GENERATE(44100.0, 48000.0, 96000.0);
  CAPTURE(rate);
  const int open = ticksFor(rate, 0.002);
  const int ticks = ticksFor(rate, 0.04);
  auto steady = [](int) {
    VoiceControl c;
    c.params.filter.p[0] = 0.25f;
    return c;
  };
  auto burst = [open](int t) {
    VoiceControl c;
    c.params.filter.p[0] = t < open ? 0.25f : 0.0f;
    return c;
  };
  InputVoice reference(rate);
  reference.voice.setFilterEngine(1);
  reference.voice.trigger(60, 1.0f);
  const Rendered full = reference.render(ticks, steady, true);
  InputVoice probe(rate);
  probe.voice.setFilterEngine(1);
  probe.voice.trigger(60, 1.0f);
  const Rendered cut = probe.render(ticks, burst, true);
  const size_t edge = size_t(probe.voice.latencySamples() + open * 16);
  for (size_t s = 0; s < edge; ++s) REQUIRE(cut.l[s] == full.l[s]);  // the onset passes
  REQUIRE(full.l[edge - 1] > 0.01f);
  for (size_t s = edge; s < cut.l.size(); ++s) REQUIRE(cut.l[s] == 0.0f);  // then the gain is 0
  REQUIRE(full.l[edge] > 0.01f);
}
