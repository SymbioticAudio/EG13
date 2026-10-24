#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "alloc_counter.h"
#include "engine/engine_registry.h"
#include "engine/part_fx_engine.h"
#include "part/part.h"
#include "part/part_fx.h"
#include "stmlib/utils/random.h"
#include "test_engines.h"
#include "test_helpers.h"
#include "voice/processor_factory.h"

using namespace eg13;

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

struct Stereo {
  std::vector<float> l, r;
  explicit Stereo(size_t n = 0) : l(n, 0.0f), r(n, 0.0f) {}
};

// Voices: a 0.5 sine at the note while the gate is on, at 32 kHz (converted: the voices have a
// latency) or at the host rate.
SourceSpec sine32k() { return {std::make_unique<test::SineEngine>(), 32000.0, 16}; }
SourceSpec sineHost() { return {std::make_unique<test::SineEngine>(), 0.0, 16}; }

// Part FX test engines.
// Outputs P1 on both channels, whatever its input.
class ParamDcEngine final : public VoiceEngine {
 public:
  void prepare(double, int) override {}
  void reset() override {}
  void noteOn(float, float) override {}
  void noteOff() override {}
  void setNote(float) override {}
  void process(const float* const*, float* const* out, const ParamValues& p, int n) override {
    for (int i = 0; i < n; ++i) out[0][i] = out[1][i] = p.p[0];
  }
  const EngineInfo& info() const override {
    static const EngineInfo kInfo = {"paramdc", "Param DC", 0, nullptr, true, false, 0, nullptr};
    return kInfo;
  }
};

// Records the extremes of every P it is given since the last clear().
class SpyEngine final : public VoiceEngine {
 public:
  void prepare(double, int) override {}
  void reset() override {}
  void noteOn(float, float) override {}
  void noteOff() override {}
  void setNote(float) override {}
  void process(const float* const* in, float* const* out, const ParamValues& p, int n) override {
    for (int k = 0; k < kPartFxParamCount; ++k) {
      lo[k] = std::min(lo[k], p.p[k]);
      hi[k] = std::max(hi[k], p.p[k]);
    }
    for (int i = 0; i < n; ++i) {
      out[0][i] = in ? in[0][i] : 0.0f;
      out[1][i] = in ? in[1][i] : 0.0f;
    }
  }
  const EngineInfo& info() const override {
    static const EngineInfo kInfo = {"spy", "Spy", 0, nullptr, true, false, 0, nullptr};
    return kInfo;
  }
  void clear() {
    for (int k = 0; k < kPartFxParamCount; ++k) {
      lo[k] = 1e9f;
      hi[k] = -1e9f;
    }
  }
  float lo[kPartFxParamCount], hi[kPartFxParamCount];
};
SpyEngine* gSpy = nullptr;

struct NoteLog {
  int noteOns = 0;
  float lastNote = -1.0f;  // from noteOn or setNote
};
NoteLog gLog[3];

// Passes its input through and logs its notes.
class LoggingEngine final : public VoiceEngine {
 public:
  explicit LoggingEngine(NoteLog* log) : log_(log) {}
  void prepare(double, int) override {}
  void reset() override {}
  void noteOn(float note, float) override {
    ++log_->noteOns;
    log_->lastNote = note;
  }
  void noteOff() override {}
  void setNote(float note) override { log_->lastNote = note; }
  void process(const float* const* in, float* const* out, const ParamValues&, int n) override {
    for (int i = 0; i < n; ++i) {
      out[0][i] = in ? in[0][i] : 0.0f;
      out[1][i] = in ? in[1][i] : 0.0f;
    }
  }
  const EngineInfo& info() const override {
    static const EngineInfo kInfo = {"logging", "Logging", 0, nullptr, true, false, 0, nullptr};
    return kInfo;
  }

 private:
  NoteLog* log_;
};

// Part FX factories. Index 0 is always Off.
std::vector<SourceSpec> offOnlyFx() {  // the real Off, padded to its own whole-block latency
  std::vector<SourceSpec> all;
  all.push_back(makePartFx(int(PartFxEngine::Off)));
  return all;
}
std::vector<SourceSpec> paramDcFx() {  // no latency: Off unpadded, a host-rate engine
  std::vector<SourceSpec> all;
  all.push_back(makeOff());
  all.push_back({std::make_unique<ParamDcEngine>(), 0.0, 16});
  return all;
}
std::vector<SourceSpec> loggingFx() {
  for (auto& log : gLog) log = NoteLog{};
  std::vector<SourceSpec> all;
  all.push_back(makeOff());
  all.push_back({std::make_unique<LoggingEngine>(&gLog[1]), 0.0, 16});
  all.push_back({std::make_unique<LoggingEngine>(&gLog[2]), 0.0, 16});
  return all;
}
// Pass-through effects: Echo converted at 32 kHz, and a Gain (out = 4 x P1 x in), in whole blocks.
std::vector<SourceSpec> passFx() {
  std::vector<SourceSpec> all;
  all.push_back(makePartFx(int(PartFxEngine::Off)));
  all.push_back({std::make_unique<test::EchoEngine>(), 32000.0, 16, true});
  all.push_back({std::make_unique<test::GainEngine>(), 0.0, 16, true});
  return all;
}
// A pass-through with its own latency (60 samples, plus the whole-block FIFO), at the host rate.
std::vector<SourceSpec> delayFx() {
  std::vector<SourceSpec> all;
  all.push_back(makePartFx(int(PartFxEngine::Off)));
  all.push_back({std::make_unique<test::FixedLatencyEngine>(), 0.0, 16, true});
  return all;
}
std::vector<SourceSpec> spyFx() {  // Off, then a host-rate spy
  std::vector<SourceSpec> all;
  all.push_back(makeOff());
  auto spy = std::make_unique<SpyEngine>();
  gSpy = spy.get();
  spy->clear();
  all.push_back({std::move(spy), 0.0, 16});
  return all;
}
std::vector<SourceSpec> nanFx() {
  std::vector<SourceSpec> all;
  all.push_back(makeOff());
  all.push_back({std::make_unique<test::NanUntilResetEngine>(40), 0.0, 16});
  return all;
}

// Renders n samples through the part's modulated path in host blocks of `block`, splitting the
// blocks at each event sample, where at(sample) runs first. input: the part's Input (nullable).
Stereo render(Part& part, int n, int block, const PartParams& params, const Stereo* input = nullptr,
              const std::vector<int>& events = {}, const std::function<void(int)>& at = nullptr,
              PartParams* live = nullptr) {
  Stereo out(static_cast<size_t>(n));
  size_t next = 0;
  for (int done = 0; done < n;) {
    while (next < events.size() && events[next] <= done) {
      if (events[next] == done && at) at(done);
      ++next;
    }
    int count = std::min(block, n - done);
    if (next < events.size()) count = std::min(count, events[next] - done);
    float* o[2] = {out.l.data() + done, out.r.data() + done};
    const float* in[2] = {input ? input->l.data() + done : nullptr, input ? input->r.data() + done : nullptr};
    VoiceInputs inputs;
    inputs.input = input ? in : nullptr;
    part.process(inputs, o, live ? *live : params, count);
    done += count;
  }
  return out;
}

Stereo sineSignal(size_t n, double hz, double rate, float amplitude) {
  Stereo s(n);
  for (size_t i = 0; i < n; ++i) s.l[i] = s.r[i] = amplitude * float(std::sin(6.283185307179586 * hz * double(i) / rate));
  return s;
}

Stereo noiseSignal(size_t n) {
  Stereo s(n);
  uint32_t x = 12345u;
  for (size_t i = 0; i < n; ++i) {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s.l[i] = float(x >> 8) / 8388608.0f - 1.0f;
    s.r[i] = -s.l[i];
  }
  return s;
}

float maxStep(const std::vector<float>& x, size_t from, size_t to) {
  float step = 0.0f;
  for (size_t i = std::max<size_t>(from, 1); i < to; ++i) step = std::max(step, std::fabs(x[i] - x[i - 1]));
  return step;
}

PartSettings fxSettings(int engine, PartFxInput input = PartFxInput::PartMix) {
  PartSettings s;
  s.partFxEngine = engine;
  s.partFxInput = int(input);
  return s;
}

const char* qualityName(ConverterQuality q) { return q == ConverterQuality::Live ? "Live" : "Studio"; }

}  // namespace

TEST_CASE("Part latency includes the Part FX slot") {
  const double rate = GENERATE(44100.0, 48000.0, 96000.0);
  const ConverterQuality q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  CAPTURE(rate, qualityName(q));
  Part part(sine32k);
  part.prepare(rate, kBlock, Voice::Alignment::Notes, q);
  Voice voice(sine32k());
  voice.prepare(rate, kBlock, Voice::Alignment::Notes, q);
  PartFx fx(makeAllPartFx());
  fx.prepare(rate, kBlock, voice.latencySamples(), q);
  std::printf("Part latency %s %.0f Hz: voices %d + Part FX %d = %d samples\n", qualityName(q), rate,
              voice.latencySamples(), fx.latencySamples(), part.latencySamples());
  REQUIRE(voice.latencySamples() > 0);
  REQUIRE(fx.latencySamples() > 0);
  REQUIRE(part.voiceLatencySamples() == voice.latencySamples());
  REQUIRE(part.partFx().latencySamples() == fx.latencySamples());
  REQUIRE(part.latencySamples() == voice.latencySamples() + fx.latencySamples());
}

TEST_CASE("Part FX Off passes the part mix aligned") {
  // Off pads to the slot latency: the part's output is its voices' mix (a part whose Part FX can
  // only be an unpadded Off) delayed by the Part FX latency, sample for sample, on both paths.
  const double rate = GENERATE(44100.0, 48000.0, 96000.0);
  const ConverterQuality q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  CAPTURE(rate, qualityName(q));
  const int n = int(rate * 0.1);
  for (const bool modulated : {true, false}) {
    CAPTURE(modulated);
    Stereo out[2];
    int latency = 0;
    for (int k = 0; k < 2; ++k) {
      Part part(sine32k, makeAllModEngines, k == 0 ? makeOffOnly : makeAllPartFx);
      part.prepare(rate, kBlock, Voice::Alignment::Notes, q);
      part.applySettings(fxSettings(int(PartFxEngine::Off), PartFxInput::Both));
      if (k == 1) latency = part.partFx().latencySamples();
      part.noteOn(69, 1.0f);
      if (modulated) {
        out[k] = render(part, n, 64, PartParams{});
      } else {
        out[k] = Stereo(size_t(n));
        const Stereo input = noiseSignal(size_t(n));  // the Input never leaks through Off
        for (int done = 0; done < n; done += 64) {
          float* o[2] = {out[k].l.data() + done, out[k].r.data() + done};
          const float* in[2] = {input.l.data() + done, input.r.data() + done};
          part.process(VoiceInputs{in, nullptr}, o, SlotParams{}, std::min(64, n - done));
        }
      }
    }
    REQUIRE(latency > 0);
    REQUIRE(test::rms(out[0].l, 0, size_t(n) / 2) > 0.1);
    if (!modulated) {  // the unpadded part is its voice, sample for sample
      Voice voice(sine32k());
      voice.prepare(rate, kBlock, Voice::Alignment::Notes, q);
      voice.trigger(69, 1.0f);
      Stereo alone(static_cast<size_t>(n));
      for (int done = 0; done < n; done += 64) {
        float* o[2] = {alone.l.data() + done, alone.r.data() + done};
        voice.process(VoiceInputs{}, o, SlotParams{}, std::min(64, n - done));
      }
      REQUIRE(out[0].l == alone.l);
      REQUIRE(out[0].r == alone.r);
    }
    for (int s = 0; s < latency; ++s) REQUIRE(out[1].l[size_t(s)] == 0.0f);
    for (int s = latency; s < n; ++s) {
      INFO("sample " << s);
      REQUIRE(out[1].l[size_t(s)] == out[0].l[size_t(s - latency)]);
      REQUIRE(out[1].r[size_t(s)] == out[0].r[size_t(s - latency)]);
    }
  }
}

TEST_CASE("Off ignores the input selector") {
  const int n = 24000;
  const Stereo noise = noiseSignal(size_t(n));
  for (const PartFxInput mode : {PartFxInput::PartMix, PartFxInput::Input, PartFxInput::Both}) {
    CAPTURE(int(mode));
    Part part(sine32k);
    part.prepare(kRate, kBlock);
    part.applySettings(fxSettings(int(PartFxEngine::Off), mode));
    const Stereo off = render(part, n, kBlock, PartParams{}, &noise);
    for (int s = 0; s < n; ++s) {
      INFO("sample " << s);
      REQUIRE(off.l[size_t(s)] == 0.0f);
      REQUIRE(off.r[size_t(s)] == 0.0f);
    }
    // The same Input reaches an effect: Reverb at its defaults (Amount 50 %).
    part.reset();
    part.applySettings(fxSettings(int(PartFxEngine::Reverb), mode));
    PartParams params;
    params.partFx = test::defaultParams(partFxEngineInfo(int(PartFxEngine::Reverb)));
    const Stereo reverb = render(part, n, kBlock, params, &noise);
    if (mode == PartFxInput::PartMix) {
      REQUIRE(test::rms(reverb.l, 0, size_t(n)) == 0.0);
    } else {
      REQUIRE(test::rms(reverb.l, size_t(n) / 2, size_t(n)) > 0.05);
    }
  }
}

TEST_CASE("Input mode with no Input keeps the voices and adds silence") {
  // A pass-through effect (Echo at 32 kHz) gets silence; the voices bypass it, delayed by the
  // slot latency: the part sounds exactly as with Off.
  const int n = 24000;
  Stereo off;
  {
    Part part(sine32k, makeAllModEngines, passFx);
    part.prepare(kRate, kBlock);
    part.applySettings(fxSettings(0, PartFxInput::Input));
    part.noteOn(69, 1.0f);
    off = render(part, n, kBlock, PartParams{});
  }
  REQUIRE(test::rms(off.l, size_t(n) / 2, size_t(n)) > 0.2);
  {
    Part part(sine32k, makeAllModEngines, passFx);
    part.prepare(kRate, kBlock);
    part.applySettings(fxSettings(1, PartFxInput::Input));
    part.noteOn(69, 1.0f);
    const Stereo input = render(part, n, kBlock, PartParams{});
    for (int s = 0; s < n; ++s) {
      INFO("sample " << s);
      REQUIRE(input.l[size_t(s)] == off.l[size_t(s)]);
      REQUIRE(input.r[size_t(s)] == off.r[size_t(s)]);
    }
  }
  // Every real effect: the voices keep their level (the effect hears silence).
  for (int e = 1; e < kNumPartFxEngines; ++e) {
    CAPTURE(e);
    stmlib::Random::Seed(1);
    Part plain(sine32k);
    plain.prepare(kRate, kBlock);
    plain.applySettings(fxSettings(int(PartFxEngine::Off)));
    plain.noteOn(69, 1.0f);
    const Stereo dry = render(plain, n, kBlock, PartParams{});
    Part part(sine32k);
    part.prepare(kRate, kBlock);
    part.applySettings(fxSettings(e, PartFxInput::Input));
    part.noteOn(69, 1.0f);
    PartParams params;
    params.partFx = test::defaultParams(partFxEngineInfo(e));
    const Stereo wet = render(part, n, kBlock, params);
    double error = 0.0;
    for (int s = 0; s < n; ++s) error = std::max(error, double(std::fabs(wet.l[size_t(s)] - dry.l[size_t(s)])));
    std::printf("Input mode, no Input, %s: largest difference from Off %.3g\n", partFxEngineInfo(e).displayName, error);
    REQUIRE(error < 1e-4);
  }
}

TEST_CASE("Input into Part FX leaves the part with Dry's latency") {
  // An impulse on the Input, through a pass-through effect (Echo, converted at 32 kHz), peaks at
  // the part's latency: the voices' latency (the Input waits for them) plus the slot's.
  const double rate = GENERATE(44100.0, 48000.0, 96000.0);
  const ConverterQuality q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  CAPTURE(rate, qualityName(q));
  for (const PartFxInput mode : {PartFxInput::Input, PartFxInput::Both}) {
    CAPTURE(int(mode));
    Part part(sine32k, makeAllModEngines, passFx);
    part.prepare(rate, kBlock, Voice::Alignment::Notes, q);
    part.applySettings(fxSettings(1, mode));
    const int n = part.latencySamples() + 2048;
    Stereo impulse(static_cast<size_t>(n));
    impulse.l[0] = impulse.r[0] = 1.0f;
    const Stereo out = render(part, n, 64, PartParams{}, &impulse);
    INFO("voices " << part.voiceLatencySamples() << " + Part FX " << part.partFx().latencySamples());
    REQUIRE(part.voiceLatencySamples() > 0);
    REQUIRE(test::peakIndex(out.l) == part.latencySamples());
    // A real peak, band-limited by the 32 kHz round trip (about 32000 / rate x 0.7 high).
    REQUIRE(out.l[size_t(part.latencySamples())] > 0.5f * float(std::min(1.0, 32000.0 / rate)));
  }
}

TEST_CASE("Part FX controls act after the voices' latency") {
  // A host P1 step at tick t reaches the effect (which outputs P1) at t x 16 + the voices'
  // latency, to the sample. The slot adds no latency here.
  const double rate = GENERATE(44100.0, 48000.0, 96000.0);
  const ConverterQuality q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  CAPTURE(rate, qualityName(q));
  Part part(sine32k, makeAllModEngines, paramDcFx);
  part.prepare(rate, kBlock, Voice::Alignment::Notes, q);
  part.applySettings(fxSettings(1));
  const int voices = part.voiceLatencySamples();
  REQUIRE(part.partFx().latencySamples() == 0);
  REQUIRE(voices > 16);
  REQUIRE(voices % 16 != 0);  // the step lands inside a tick
  const int tick = 50;
  const int n = tick * 16 + voices + 256;
  PartParams params;
  params.partFx.p[0] = 0.25f;
  const Stereo out = render(part, n, 16, params, nullptr, {tick * 16}, [&](int) { params.partFx.p[0] = 0.75f; },
                            &params);
  const int edge = tick * 16 + voices;
  for (int s = 0; s < n; ++s) {
    INFO("sample " << s << " edge " << edge);
    REQUIRE(out.l[size_t(s)] == (s < edge ? 0.25f : 0.75f));
  }
}

TEST_CASE("Every note-on is one Part FX trigger; legato glides are none") {
  auto make = [](VoiceMode mode, bool alwaysOn) {
    auto part = std::make_unique<Part>(sine32k, makeAllModEngines, loggingFx);
    part->prepare(kRate, kBlock);
    PartSettings s = fxSettings(1);
    s.voiceMode = mode;
    s.alwaysOn = alwaysOn;
    part->applySettings(s);
    return part;
  };
  PartParams params;
  SECTION("Poly: each note, after the voices' latency; a chord in one tick is one") {
    auto part = make(VoiceMode::Poly, false);
    const int latency = part->voiceLatencySamples();
    part->noteOn(60, 1.0f);
    render(*part, 16, 16, params);  // the tick that carries the trigger
    render(*part, latency - 16, 16, params);
    REQUIRE(gLog[1].noteOns == 0);  // not yet: the note's sound has not reached the Part FX
    render(*part, 16, 16, params);
    REQUIRE(gLog[1].noteOns == 1);
    part->noteOn(64, 1.0f);
    render(*part, latency + 32, 16, params);
    part->noteOn(67, 1.0f);
    part->noteOn(71, 1.0f);  // same tick
    render(*part, latency + 32, 16, params);
    REQUIRE(gLog[1].noteOns == 3);
    REQUIRE(gLog[2].noteOns == 0);
  }
  SECTION("Legato: a glide is no trigger; a note after every key was up is") {
    auto part = make(VoiceMode::Legato, false);
    const int wait = part->voiceLatencySamples() + 32;
    part->noteOn(60, 1.0f);
    render(*part, wait, 16, params);
    part->noteOn(64, 1.0f);  // legato
    render(*part, wait, 16, params);
    REQUIRE(gLog[1].noteOns == 1);
    part->noteOff(64);
    part->noteOff(60);
    render(*part, wait, 16, params);
    part->noteOn(67, 1.0f);
    render(*part, wait, 16, params);
    REQUIRE(gLog[1].noteOns == 2);
  }
  SECTION("Mono: every note retriggers") {
    auto part = make(VoiceMode::Mono, false);
    const int wait = part->voiceLatencySamples() + 32;
    part->noteOn(60, 1.0f);
    render(*part, wait, 16, params);
    part->noteOn(64, 1.0f);
    render(*part, wait, 16, params);
    REQUIRE(gLog[1].noteOns == 2);
  }
  SECTION("Always on: every key press") {
    auto part = make(VoiceMode::Poly, true);
    const int wait = part->voiceLatencySamples() + 32;
    render(*part, wait, 16, params);
    REQUIRE(gLog[1].noteOns == 0);  // switching Always on is no key press
    part->noteOn(60, 1.0f);
    render(*part, wait, 16, params);
    part->noteOn(64, 1.0f);  // over the held key
    render(*part, wait, 16, params);
    part->noteOff(64);
    render(*part, wait, 16, params);
    REQUIRE(gLog[1].noteOns == 2);
  }
}

TEST_CASE("A Part FX type change does not retrigger the new engine") {
  // G26: the trigger is a noteOn and a noteOff, so the slot does not hand a held gate to the
  // engine it fades to.
  Part part(sine32k, makeAllModEngines, loggingFx);
  part.prepare(kRate, kBlock);
  part.applySettings(fxSettings(1));
  PartParams params;
  part.noteOn(60, 1.0f);
  render(part, part.voiceLatencySamples() + 64, 16, params);
  REQUIRE(gLog[1].noteOns == 1);
  part.applySettings(fxSettings(2));  // the note still held
  render(part, 4800, 16, params);
  REQUIRE(part.partFx().engine() == 2);
  REQUIRE(gLog[2].noteOns == 0);
  REQUIRE(gLog[2].lastNote == 60.0f);  // it still gets the note
}

TEST_CASE("The Resonator FX note follows the last voice, Base note before any note and on Always-on") {
  PartParams params;
  PartSettings s = fxSettings(1);
  s.baseNote = 50;
  s.transpose = 2;
  s.bendRange = 2;
  SECTION("Poly") {
    Part part(sine32k, makeAllModEngines, loggingFx);
    part.prepare(kRate, kBlock);
    part.applySettings(s);
    const int wait = part.voiceLatencySamples() + 32;
    render(part, wait, 16, params);
    REQUIRE(gLog[1].lastNote == 52.0f);  // Base note + Transpose
    part.noteOn(64, 1.0f);
    render(part, 16, 16, params);
    REQUIRE(gLog[1].lastNote == 52.0f);  // after the voices' latency
    render(part, wait, 16, params);
    REQUIRE(gLog[1].lastNote == 66.0f);
    part.noteOn(70, 1.0f);
    render(part, wait, 16, params);
    REQUIRE(gLog[1].lastNote == 72.0f);
    part.noteOff(70);  // still the most recently started voice
    render(part, wait, 16, params);
    REQUIRE(gLog[1].lastNote == 72.0f);
    part.pitchBend(0.5f);  // + 1 semitone
    render(part, wait, 16, params);
    REQUIRE(gLog[1].lastNote == 73.0f);
  }
  SECTION("Always on") {
    s.alwaysOn = true;
    Part part(sine32k, makeAllModEngines, loggingFx);
    part.prepare(kRate, kBlock);
    part.applySettings(s);
    const int wait = part.voiceLatencySamples() + 32;
    render(part, wait, 16, params);
    REQUIRE(gLog[1].lastNote == 52.0f);
    part.noteOn(67, 1.0f);
    render(part, wait, 16, params);
    REQUIRE(gLog[1].lastNote == 69.0f);
    part.noteOn(60, 1.0f);
    render(part, wait, 16, params);
    REQUIRE(gLog[1].lastNote == 62.0f);
    part.noteOff(60);
    render(part, wait, 16, params);
    REQUIRE(gLog[1].lastNote == 69.0f);  // back to the held key
    part.noteOff(67);
    render(part, wait, 16, params);
    REQUIRE(gLog[1].lastNote == 52.0f);  // no key held: the Base note
  }
}

TEST_CASE("Switching Part FX type while sounding crossfades and keeps alignment") {
  // Off -> Echo (32 kHz, converted) -> Gain at unity -> Off while a note sounds: the output stays
  // on the Off part's, within less than a sample. Then
  // Off -> Gain x 2: unchanged for the slot latency, then a 10 ms crossfade to twice the level.
  const double rate = GENERATE(44100.0, 48000.0, 96000.0);
  const ConverterQuality q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  CAPTURE(rate, qualityName(q));
  const int n = int(rate * 0.6);
  const int fade = int(std::ceil(rate * kSlotFadeSeconds));
  const std::vector<int> switches = {int(rate * 0.1), int(rate * 0.2), int(rate * 0.3), int(rate * 0.4)};
  const int engines[] = {1, 2, 0, 2};
  auto play = [&](bool change, float gain) {
    Part part(sine32k, makeAllModEngines, passFx);
    part.prepare(rate, kBlock, Voice::Alignment::Notes, q);
    part.applySettings(fxSettings(0));
    part.noteOn(81, 1.0f);  // 880 Hz
    PartParams params;
    params.partFx.p[0] = gain / 4.0f;  // Gain's P1
    int k = 0;
    const Stereo out = render(part, n, 100, params, nullptr, switches, [&](int) {
      if (change) part.applySettings(fxSettings(engines[k]));
      ++k;
    });
    return std::make_pair(out, part.partFx().latencySamples());
  };
  const auto reference = play(false, 1.0f);
  const auto switched = play(true, 1.0f);
  const int latency = reference.second;
  // Settled (after each crossfade) the output is the Off part's; during a crossfade the engine
  // fading in starts empty, so its converters' onset shows, without a click.
  double settled = 0.0, fading = 0.0;
  for (int k = 0; k < 3; ++k) {
    for (int s = switches[size_t(k)]; s < switches[size_t(k) + 1]; ++s) {
      const double e = std::fabs(switched.first.l[size_t(s)] - reference.first.l[size_t(s)]);
      if (s > switches[size_t(k)] + latency + fade + 1) {
        settled = std::max(settled, e);
      } else {
        fading = std::max(fading, e);
      }
    }
  }
  std::printf("Part FX type switches %s %.0f Hz: largest difference from Off %.4f settled, %.4f fading\n",
              qualityName(q), rate, settled, fading);
  // One sample off would be about 0.5 x 2 pi x 880 / rate. The converted Echo itself sits a
  // fraction of a sample off at 44.1 kHz (its latency is reported in whole samples): allow 0.7.
  const double oneSample = 0.5 * 6.283185307179586 * 880.0 / rate;
  REQUIRE(settled < 0.7 * oneSample);
  REQUIRE(maxStep(switched.first.l, size_t(switches[0]), size_t(switches[3])) <=
          1.05f * maxStep(reference.first.l, size_t(switches[0]), size_t(switches[3])));
  // Off -> Gain x 2 at the fourth switch.
  const auto doubled = play(true, 2.0f);
  const std::vector<float>& ref = reference.first.l;
  const std::vector<float>& out = doubled.first.l;
  const int at = switches[3];
  for (int s = at - 100; s < at + latency; ++s) REQUIRE(out[size_t(s)] == ref[size_t(s)]);
  for (int s = at + latency + fade + 2; s < n; ++s) REQUIRE(std::fabs(out[size_t(s)] - 2.0f * ref[size_t(s)]) < 1e-5f);
  // In between, the weight of the doubled signal rises steadily from 0 to 1.
  float weight = 0.0f;
  for (int s = at + latency; s <= at + latency + fade; ++s) {
    if (std::fabs(ref[size_t(s)]) < 0.1f) continue;
    const float w = out[size_t(s)] / ref[size_t(s)] - 1.0f;
    REQUIRE(w >= weight - 1e-3f);
    weight = w;
  }
  REQUIRE(weight > 0.9f);
  REQUIRE(maxStep(out, size_t(at), size_t(n)) <= 2.0f * maxStep(ref, size_t(at), size_t(n)) * 1.05f);
}

// I1: a type change gives the new engine its own P's from its first sample, not the previous
// engine's, which are still queued for the voices' latency. The previous engine's P's are 0; the
// new engine's P at `index` is `value`. Voices sound throughout (Off, then the spy).
void requireNewEngineNeverSeesOldP(int index, float value, ConverterQuality q, double rate, int switchTick) {
  CAPTURE(index, value, qualityName(q), rate, switchTick);
  PartFx fx(spyFx());
  const int voices = 268 * int(rate / 48000.0) + 7;  // not a multiple of 16
  fx.prepare(rate, 16, voices, q);
  const int ticks = switchTick + 100 + voices / 16;
  std::vector<float> mono(16);
  const Stereo mix = sineSignal(size_t(ticks) * 16, 440.0, rate, 0.5f);
  ParamValues oldP{}, newP{};
  newP.p[index] = value;
  for (int t = 0; t < ticks; ++t) {
    const bool after = t >= switchTick;
    if (t == switchTick) {
      fx.setEngine(1);  // as Part::applySettings does, just before the tick's controls
      gSpy->clear();
    }
    fx.beginTick(after ? newP : oldP, 60.0f, false);
    const float* m[2] = {mix.l.data() + t * 16, mix.r.data() + t * 16};
    float l[16], r[16];
    float* o[2] = {l, r};
    fx.process(m, nullptr, o, 16);
  }
  REQUIRE(gSpy->hi[index] >= gSpy->lo[index]);  // the spy ran
  REQUIRE(gSpy->lo[index] == value);
  REQUIRE(gSpy->hi[index] == value);
  REQUIRE(fx.appliedParams().p[index] == value);
}

TEST_CASE("After a Part FX type change Granular Delay never sees the old engine's Quality") {
  const double rate = GENERATE(44100.0, 48000.0, 96000.0);
  const ConverterQuality q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  const int tick = GENERATE(20, 21);
  requireNewEngineNeverSeesOldP(9, 1.0f / 3.0f, q, rate, tick);  // Granular Delay P10: Vintage digital
}

TEST_CASE("After a Part FX type change Granular never sees the old engine's Mode or Quality") {
  const double rate = GENERATE(44100.0, 48000.0, 96000.0);
  const ConverterQuality q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  requireNewEngineNeverSeesOldP(9, 1.0f, q, rate, 20);   // Granular P10: Spectral
  requireNewEngineNeverSeesOldP(10, 1.0f, q, rate, 20);  // Granular P11: the last Quality
}

TEST_CASE("After a Part FX type change Reverb and Resonator pots start at their own values") {
  const ConverterQuality q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  for (int index : {0, 1, 2, 3}) requireNewEngineNeverSeesOldP(index, 0.9f, q, 48000.0, 20);
}

TEST_CASE("A Part FX type change replaces only the queued P's, not notes or triggers") {
  // A trigger queued before the change still acts voiceLatency samples after its tick.
  gLog[1] = NoteLog{};
  PartFx fx([] {
    std::vector<SourceSpec> all;
    all.push_back(makeOff());
    all.push_back({std::make_unique<LoggingEngine>(&gLog[1]), 0.0, 16});
    return all;
  }());
  const int voices = 200;
  fx.prepare(48000.0, 16, voices);
  const Stereo mix = sineSignal(16 * 40, 440.0, 48000.0, 0.5f);
  std::vector<int> noteOnsAfter;
  for (int t = 0; t < 40; ++t) {
    if (t == 5) fx.setEngine(1);
    // The trigger tick (t = 4) began before the change; its note-on acts at sample 264, inside tick 16.
    fx.beginTick(ParamValues{}, 60.0f + float(t == 4 ? 7 : 0), t == 4);
    const float* m[2] = {mix.l.data() + t * 16, mix.r.data() + t * 16};
    float l[16], r[16];
    float* o[2] = {l, r};
    fx.process(m, nullptr, o, 16);
    noteOnsAfter.push_back(gLog[1].noteOns);
  }
  // The engine received the trigger (G26 notwithstanding, the queue kept it) exactly once, at sample 264.
  REQUIRE(noteOnsAfter[15] == 0);
  REQUIRE(noteOnsAfter[16] == 1);
  REQUIRE(noteOnsAfter.back() == 1);
}

TEST_CASE("A Part FX type change requested during a fade neither doubles nor drops the voices") {
  // Off -> Gain (unity) -> Off -> Gain within 5 ms each: every switch lands inside the previous
  // one's hold or crossfade. The output stays on the Off part's.
  const double rate = GENERATE(44100.0, 48000.0, 96000.0);
  const ConverterQuality q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  CAPTURE(rate, qualityName(q));
  const int n = int(rate * 0.6);
  const int gap = int(rate * 0.005);
  const int start = int(rate * 0.1);
  const std::vector<int> switches = {start, start + gap, start + 2 * gap, start + 3 * gap, start + 4 * gap};
  const int engines[] = {2, 0, 2, 0, 2};
  auto play = [&](bool change) {
    Part part(sine32k, makeAllModEngines, passFx);
    part.prepare(rate, kBlock, Voice::Alignment::Notes, q);
    part.applySettings(fxSettings(0));
    part.noteOn(81, 1.0f);
    PartParams params;
    params.partFx.p[0] = 0.25f;  // Gain at unity
    int k = 0;
    return render(part, n, 100, params, nullptr, switches, [&](int) {
      if (change) part.applySettings(fxSettings(engines[k]));
      ++k;
    });
  };
  const Stereo reference = play(false);
  const Stereo switched = play(true);
  double worst = 0.0;
  for (int s = start; s < n; ++s) worst = std::max(worst, double(std::fabs(switched.l[size_t(s)] - reference.l[size_t(s)])));
  std::printf("Part FX request during a fade %s %.0f Hz: largest difference from Off %.5f\n", qualityName(q), rate, worst);
  REQUIRE(worst < 1e-3);
  REQUIRE(test::rms(switched.l, size_t(n) - 4800, size_t(n)) > 0.3);
}

TEST_CASE("A NaN Part FX output restarts the engine and mutes that slice") {
  // The effect (a pass-through) blows up on its 41st call and stays NaN until it is reset.
  const int n = 4800;
  auto play = [&](int engine) {
    Part part(sineHost, makeAllModEngines, nanFx);
    part.prepare(kRate, kBlock);
    part.applySettings(fxSettings(engine));
    part.noteOn(69, 1.0f);
    return render(part, n, 16, PartParams{});
  };
  const Stereo off = play(0);
  const Stereo out = play(1);
  int muted = 0, first = -1;
  for (int s = 0; s < n; ++s) {
    REQUIRE(std::isfinite(out.l[size_t(s)]));
    REQUIRE(std::isfinite(out.r[size_t(s)]));
    if (out.l[size_t(s)] == off.l[size_t(s)]) continue;
    if (first < 0) first = s;
    INFO("sample " << s << " first " << first);
    REQUIRE(out.l[size_t(s)] == 0.0f);  // silence
    REQUIRE(s < first + 16);            // for that slice only
    ++muted;
  }
  REQUIRE(muted > 8);  // one 16-sample slice, less any samples the sine had at zero
  REQUIRE(test::rms(out.l, size_t(first) + 16, size_t(n)) > 0.2);  // the engine plays again
}

TEST_CASE("Part FX does not depend on host block slicing") {
  // Every real engine at 44.1, 48 and 96 kHz, in Both mode with an Input, with notes and a P
  // change: host blocks of 1, 7 and 129 samples give the 128-sample output exactly.
  const double rate = GENERATE(44100.0, 48000.0, 96000.0);
  CAPTURE(rate);
  const int n = int(rate * 0.25);
  const Stereo input = sineSignal(size_t(n), 330.0, rate, 0.3f);
  const std::vector<int> events = {int(rate * 0.05) / 16 * 16, int(rate * 0.1) / 16 * 16, int(rate * 0.15) / 16 * 16};
  for (int e = 1; e < kNumPartFxEngines; ++e) {
    CAPTURE(e);
    PartParams params;
    auto play = [&](int block) {
      stmlib::Random::Seed(1);
      params.partFx = test::defaultParams(partFxEngineInfo(e));
      params.partFx.p[0] = 0.7f;  // mostly wet
      params.partFx.p[4] = 0.8f;  // Granular Density: grains at random
      Part part(sine32k);  // the voices' latency is not a multiple of 16: controls act mid-tick
      part.prepare(rate, kBlock);
      part.applySettings(fxSettings(e, PartFxInput::Both));
      part.noteOn(57, 1.0f);
      return render(part, n, block, params, &input, events, [&](int s) {
        if (s == events[0]) part.noteOn(64, 1.0f);
        if (s == events[1]) params.partFx.p[1] = 0.2f;
        if (s == events[2]) part.noteOff(57);
      }, &params);
    };
    const Stereo reference = play(128);
    REQUIRE(test::rms(reference.l, size_t(n) / 2, size_t(n)) > 0.01);
    for (const int block : {1, 7, 129}) {
      CAPTURE(block);
      const Stereo out = play(block);
      REQUIRE(out.l == reference.l);
      REQUIRE(out.r == reference.r);
    }
  }
}

TEST_CASE("Part memory with every Part FX engine", "[memory]") {
  // G13: the Part FX share, the difference between a Part with every Part FX engine and one whose
  // Part FX can only be Off, each constructed and prepared at 48 kHz, 512.
  auto bytes = [](Part::SlotFactory partFx) {
    test::ScopedAllocationCounter counter;
    Part part(makeAllSources, makeAllFilters, makeAllShapers, makeAllModEngines, partFx);
    part.prepare(48000.0, 512);
    const size_t total = counter.bytes();
    return total;
  };
  bytes(makeAllPartFx);  // warm up first: one-time caches (the converters' setup) are not per part
  const size_t all = bytes(makeAllPartFx);
  const size_t off = bytes(offOnlyFx);
  const size_t share = all - off;
  std::printf("Part memory (48 kHz, 512): every Part FX engine %.2f MB, Off only %.2f MB; Part FX share %zu bytes (%.2f MB)\n",
              double(all) / 1048576.0, double(off) / 1048576.0, share, double(share) / 1048576.0);
  REQUIRE(all > off);
  REQUIRE(share < size_t(4) * 1024 * 1024);
}

TEST_CASE("PartFx allocates nothing in process") {
  PartFx fx(makeAllPartFx());
  fx.prepare(kRate, kBlock, 300);
  const Stereo mix = sineSignal(kBlock, 440.0, kRate, 0.5f);
  const Stereo input = sineSignal(kBlock, 330.0, kRate, 0.5f);
  Stereo out(kBlock);
  const float* m[2] = {mix.l.data(), mix.r.data()};
  const float* in[2] = {input.l.data(), input.r.data()};
  float* o[2] = {out.l.data(), out.r.data()};
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int e = 0; e < kNumPartFxEngines; ++e) {
      fx.setEngine(e);
      ParamValues p = test::defaultParams(partFxEngineInfo(e));
      for (int mode = 0; mode < kNumPartFxInputs; ++mode) {
        fx.setInput(PartFxInput(mode));
        for (int block = 0; block < 40; ++block) {
          for (int t = 0; t < kBlock / 16; ++t) fx.beginTick(p, 60.0f + float(t), t == 0);
          fx.process(m, block % 2 == 0 ? in : nullptr, o, kBlock);
        }
      }
    }
    fx.reset();
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}

TEST_CASE("A Part FX input-selector change while sounding is click-free") {
  // A pass-through effect with its own latency. With nothing on the Input, every selector change
  // leaves the part exactly as with Off: the mix the effect stops receiving is the mix the bypass
  // carries, in step. With an Input, each change fades it in or out over 10 ms.
  const double rate = GENERATE(44100.0, 48000.0, 96000.0);
  CAPTURE(rate);
  const int n = int(rate * 0.5);
  const std::vector<int> changes = {int(rate * 0.1), int(rate * 0.2), int(rate * 0.3), int(rate * 0.4)};
  const PartFxInput modes[] = {PartFxInput::Input, PartFxInput::Both, PartFxInput::PartMix, PartFxInput::Input};
  const Stereo input = sineSignal(size_t(n), 1234.0, rate, 0.4f);
  auto play = [&](int engine, bool change, const Stereo* in) {
    Part part(sine32k, makeAllModEngines, delayFx);
    part.prepare(rate, kBlock);
    part.applySettings(fxSettings(engine));
    part.noteOn(81, 1.0f);
    int k = 0;
    return render(part, n, 100, PartParams{}, in, changes, [&](int) {
      if (change) part.applySettings(fxSettings(engine, modes[k]));
      ++k;
    });
  };
  const Stereo off = play(0, false, nullptr);
  const Stereo quiet = play(1, true, nullptr);
  for (int s = 0; s < n; ++s) {
    INFO("sample " << s);
    REQUIRE(std::fabs(quiet.l[size_t(s)] - off.l[size_t(s)]) < 1e-6f);
  }
  const Stereo loud = play(1, true, &input);
  const float voiceStep = maxStep(off.l, 0, size_t(n));
  const float inputStep = maxStep(input.l, 0, size_t(n));
  const float step = maxStep(loud.l, 0, size_t(n));
  INFO("voices' step " << voiceStep << " input's step " << inputStep << " output's step " << step);
  REQUIRE(step <= 1.05f * (voiceStep + inputStep));
  // The Input is heard in Input and Both (a 0.4 sine: RMS 0.28) and not in Part mix.
  std::vector<float> heard(static_cast<size_t>(n));
  for (int s = 0; s < n; ++s) heard[size_t(s)] = loud.l[size_t(s)] - off.l[size_t(s)];
  const size_t settle = size_t(rate * 0.05);
  REQUIRE(test::rms(heard, size_t(changes[0]) + settle, size_t(changes[1])) > 0.27);
  REQUIRE(test::rms(heard, size_t(changes[1]) + settle, size_t(changes[2])) > 0.27);
  REQUIRE(test::rms(heard, size_t(changes[2]) + settle, size_t(changes[3])) < 1e-6);
}

TEST_CASE("Granular (Input) -> Off -> Granular keeps the voices' level and never doubles them") {
  // G12: in Input mode the voices bypass Granular; Off passes them too. Through both type
  // changes every 5 ms window stays within 1 dB of the voices alone.
  const int n = int(kRate * 0.6);
  const std::vector<int> changes = {int(kRate * 0.2), int(kRate * 0.4)};
  auto play = [&](bool change) {
    stmlib::Random::Seed(1);
    Part part(sine32k);
    part.prepare(kRate, kBlock);
    part.applySettings(fxSettings(change ? int(PartFxEngine::Granular) : 0, PartFxInput::Input));
    part.noteOn(69, 1.0f);
    PartParams params;
    params.partFx = test::defaultParams(partFxEngineInfo(int(PartFxEngine::Granular)));
    int k = 0;
    return render(part, n, kBlock, params, nullptr, changes, [&](int) {
      if (change) part.applySettings(fxSettings(k == 0 ? 0 : int(PartFxEngine::Granular), PartFxInput::Input));
      ++k;
    });
  };
  const Stereo voices = play(false);
  const Stereo out = play(true);
  const size_t window = size_t(kRate * 0.005);
  for (size_t s = size_t(kRate * 0.05); s + window <= size_t(n); s += window) {
    const double a = test::rms(out.l, s, s + window);
    const double b = test::rms(voices.l, s, s + window);
    INFO("window at " << s << ": " << a << " against " << b);
    REQUIRE(std::fabs(20.0 * std::log10(a / b)) < 1.0);
  }
}
