#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "alloc_counter.h"
#include "beads/dsp/processor.h"
#include "engine/beads_engine.h"
#include "engine/native_rate_wrapper.h"
#include "engine/part_fx_engine.h"
#include "stmlib/dsp/dsp.h"
#include "stmlib/utils/gate_flags.h"
#include "stmlib/utils/random.h"
#include "test_helpers.h"

using namespace eg13;
using Catch::Matchers::WithinAbs;

namespace {

ParamValues defaults() { return test::defaultParams(BeadsEngine::staticInfo()); }

// The middle of option k's step of a stepped P (16 fixed steps).
float option(int k) { return (float(k) + 0.5f) / 16.0f; }

// Each Quality's codec rate, restated (beads/dsp/processor.h:47-55 with ENABLE_HARDWARE_24K).
const double kRates[4] = {48000.0, 32000.0, 48000.0, 24000.0};

// Deterministic white noise in [-0.5, 0.5).
std::vector<float> noise(size_t n, uint32_t seed = 1) {
  std::vector<float> x(n);
  uint32_t s = seed;
  for (auto& v : x) {
    s = s * 1664525u + 1013904223u;
    v = float(s >> 8) / float(1 << 24) - 0.5f;
  }
  return x;
}

struct Stereo {
  std::vector<float> l, r;
};

// Runs the engine over `in` (both channels) in calls of `chunk` samples; before the call starting
// at sample t, params(t) gives the P's and events(t) may send notes.
template <typename Params, typename Events>
Stereo run(VoiceEngine& e, const std::vector<float>& in, int chunk, Params params, Events events) {
  const size_t n = in.size();
  Stereo out{std::vector<float>(n), std::vector<float>(n)};
  for (size_t done = 0; done < n;) {
    const int count = int(std::min<size_t>(size_t(chunk), n - done));
    events(done);
    const float* i2[2] = {in.data() + done, in.data() + done};
    float* o2[2] = {out.l.data() + done, out.r.data() + done};
    e.process(i2, o2, params(done), count);
    done += size_t(count);
  }
  return out;
}

Stereo run(VoiceEngine& e, const std::vector<float>& in, int chunk, const ParamValues& p) {
  return run(
      e, in, chunk, [&](size_t) { return p; }, [](size_t) {});
}

// The pot laws, restated from the firmware (preflight ruling G19), not taken from the adapter.
float detuneLaw(float v) {  // beads/cv_reader.h:114-129
  float detune = 2.0f * v - 1.0f;
  const float sign = detune < 0.0f ? -1.0f : 1.0f;
  detune = detune * sign * 6.0f;
  const int integral = int(detune);
  float fractional = detune - float(integral);
  const float intervals[8] = {0.0f, 1.0f, 3.0f, 4.0f, 7.0f, 12.0f, 24.0f, 24.0f};
  fractional = std::max((fractional - 0.33333333f) * 1.5f, 0.0f);
  const float a = intervals[integral];
  const float b = intervals[integral + 1];
  return stmlib::Crossfade(a, b, stmlib::SmoothStep(fractional)) * sign;
}

float attenuverterLaw(float v) {  // beads/cv_reader.h:110-112
  v -= 0.5f;
  return (2.0f / 0.47f) * v * std::max(std::fabs(v) - 0.03f, 0.0f);
}

// Bare Beads as its firmware drives it (beads/beads.cc:107-271), at the Quality's own rate in
// blocks of rate / 4000, with no converters: the memories as beads.cc:54-56 and :307-311, a
// memset processor, Init, Configure(media, false). Per block the pots pass their laws and a
// one-pole low-pass of 0.05 (the first block takes them as they are, EG13's reset rule); the
// four RandomizedParameters get {base, attenuverter, has_cv false, cv 0}, Density {base, 0}, and
// the mix pots clamp(pot * 1.005, 0, 1). Freeze per sample, output clamped to
// [-1, 0.999999]. The input stage of ruling 16 is restated: unity, clamped to +-1; Dirty tape
// soft-clips as the manual-gain path at amount 1. Seed Free: SEED unpatched, button latched;
// Seed Notes: button released, and with seed(t) a 1 ms SEED gate starts at the block holding t.
class Reference {
 public:
  explicit Reference(int quality)
      : quality_(quality),
        rate_(kRates[quality]),
        block_(int(kRates[quality]) / 4000),
        delay1_(512 * 1024),
        delay2_(256 * 1024),
        reverb_(32768),
        processor_(std::make_unique<beads::Processor>()) {
    std::memset(static_cast<void*>(processor_.get()), 0, sizeof(*processor_));
    uint8_t* buffers[3] = {delay1_.data(), delay1_.data() + 256 * 1024, delay2_.data()};
    processor_->Init(buffers, reverb_.data());
    const beads::Processor::Media media[4] = {beads::Processor::MEDIA_DIGITAL, beads::Processor::MEDIA_VINTAGE_DIGITAL,
                                              beads::Processor::MEDIA_CLEAN_TAPE,
                                              beads::Processor::MEDIA_DIRTY_TAPE};
    processor_->Configure(media[quality], false);
  }

  float stage(float x) const {
    if (quality_ == 3) return (1.0f - 0.05f) * (stmlib::SoftClip(x + 0.05f) - stmlib::SoftClip(0.05f));
    return std::clamp(x, -1.0f, 1.0f);
  }

  void block(const float* inL, const float* inR, float* outL, float* outR, const ParamValues& p, bool seeded) {
    beads::FloatFrame input[12], output[12];
    for (int i = 0; i < block_; ++i) {
      input[i].l = stage(inL[i]);
      input[i].r = stage(inR[i]);
    }
    const int params[12] = {0, 1, 2, 3, 4, 5, 6, 7, 12, 13, 14, 15};
    for (int k = 0; k < 12; ++k) {
      float v = p.p[params[k]];
      if (k == 3) v = detuneLaw(v);
      if (k >= 8) v = attenuverterLaw(v);
      if (first_) {
        pots_[k] = v;
      } else {
        pots_[k] += 0.05f * (v - pots_[k]);
      }
    }
    first_ = false;
    beads::Parameters q;
    beads::RandomizedParameter* r[4] = {&q.time, &q.size, &q.shape, &q.pitch};
    const int base[4] = {1, 2, 5, 3};
    for (int k = 0; k < 4; ++k) {
      r[k]->base = pots_[base[k]];
      r[k]->attenuverter = pots_[8 + k];
      r[k]->has_cv = false;
      r[k]->cv = 0.0f;
    }
    q.monophonic_mode = false;
    beads::ModulatedParameter density;
    density.base = pots_[4];
    density.cv = 0.0f;
    beads::Processor::MixParameters m;
    m.feedback = std::clamp(pots_[6] * 1.005f, 0.0f, 1.0f);
    m.dry_wet = std::clamp(pots_[0] * 1.005f, 0.0f, 1.0f);
    m.reverb_amount = std::clamp(pots_[7] * 1.005f, 0.0f, 1.0f);
    m.output_gate = false;
    bool freeze[12];
    std::fill(freeze, freeze + 12, p.p[8] >= 0.5f);
    const bool notes = int(p.p[10] * 16.0f) >= 1;
    stmlib::GateFlags seed[12] = {};
    if (notes) {
      if (seeded) gate_ = int(std::ceil(0.001 * rate_));
      for (int i = 0; i < 12 && i < block_; ++i) {
        const bool high = gate_ > 0;
        if (high) --gate_;
        const stmlib::GateFlags flags = stmlib::ExtractGateFlags(previous_, high);
        seed[i] = flags;
        previous_ = flags;
      }
    }
    // SEED button: latched with nothing patched (the module's start-up state, ui.cc:78), released
    // with the notes patched.
    processor_->Process(notes ? beads::SEED_BUTTON_STATE_OFF : beads::SEED_BUTTON_STATE_LOCKED, notes ? seed : nullptr, freeze, density, q, m, input, output,
                        size_t(block_));
    for (int i = 0; i < block_; ++i) {
      outL[i] = std::clamp(output[i].l, -1.0f, 0.999999f);
      outR[i] = std::clamp(output[i].r, -1.0f, 0.999999f);
    }
  }

  template <typename Params, typename Seed>
  Stereo run(const std::vector<float>& in, Params params, Seed seeded) {
    const size_t n = in.size();
    Stereo out{std::vector<float>(n), std::vector<float>(n)};
    const size_t b = size_t(block_);
    for (size_t t = 0; t + b <= n; t += b) {
      block(in.data() + t, in.data() + t, out.l.data() + t, out.r.data() + t, params(t), seeded(t));
    }
    return out;
  }

  int blockSize() const { return block_; }

 private:
  int quality_;
  double rate_;
  int block_;
  std::vector<uint8_t> delay1_, delay2_;
  std::vector<uint16_t> reverb_;
  std::unique_ptr<beads::Processor> processor_;
  float pots_[12] = {};
  bool first_ = true;
  int gate_ = 0;
  stmlib::GateFlags previous_ = stmlib::GATE_FLAG_LOW;
};

bool allFinite(const std::vector<float>& x) {
  for (float v : x)
    if (!std::isfinite(v)) return false;
  return true;
}

bool anyNonZero(const std::vector<float>& x, size_t begin = 0) {
  for (size_t i = begin; i < x.size(); ++i)
    if (x[i] != 0.0f) return true;
  return false;
}

double maxAbs(const std::vector<float>& x, size_t begin, size_t end) {
  double m = 0.0;
  for (size_t i = begin; i < std::min(end, x.size()); ++i) m = std::max(m, double(std::fabs(x[i])));
  return m;
}

double maxStep(const std::vector<float>& x, size_t begin, size_t end) {
  double m = 0.0;
  for (size_t i = begin + 1; i < std::min(end, x.size()); ++i) m = std::max(m, double(std::fabs(x[i] - x[i - 1])));
  return m;
}

// out delayed by `latency` equals ref, and out is silent before.
void requireDelayedEqual(const Stereo& out, const Stereo& ref, size_t latency) {
  const size_t n = out.l.size();
  REQUIRE(n > latency);
  REQUIRE_FALSE(anyNonZero(std::vector<float>(out.l.begin(), out.l.begin() + long(latency))));
  REQUIRE(std::vector<float>(out.l.begin() + long(latency), out.l.end()) ==
          std::vector<float>(ref.l.begin(), ref.l.begin() + long(n - latency)));
  REQUIRE(std::vector<float>(out.r.begin() + long(latency), out.r.end()) ==
          std::vector<float>(ref.r.begin(), ref.r.begin() + long(n - latency)));
}

}  // namespace

TEST_CASE("Granular Delay describes itself") {
  const EngineInfo& info = BeadsEngine::staticInfo();
  REQUIRE(std::string(info.id) == "beads");
  REQUIRE(std::string(info.displayName) == "Granular Delay");
  REQUIRE(info.usesInput);
  REQUIRE_FALSE(info.hasAux);
  REQUIRE(info.sideInputs == 0);
  REQUIRE(info.paramCount == 16);
  const char* const names[16] = {"Dry/Wet", "Time",   "Size",     "Pitch",       "Density",     "Shape",
                                 "Feedback", "Reverb", "Freeze",   "Quality",     "Seed",        "(unused)",
                                 "Time Random", "Size Random", "Shape Random", "Pitch Random"};
  for (int i = 0; i < 16; ++i) {
    INFO("P" << i + 1);
    REQUIRE(std::string(info.params[i].name) == names[i]);
    REQUIRE(info.params[i].defaultValue == kPartFxHostDefaults[i]);
    REQUIRE((info.params[i].format != nullptr) == (i != 11));
  }
  REQUIRE(info.params[4].defaultValue == 0.75f);

  REQUIRE(BeadsEngine::Digital == 0);
  REQUIRE(BeadsEngine::VintageDigital == 1);
  REQUIRE(BeadsEngine::CleanTape == 2);
  REQUIRE(BeadsEngine::DirtyTape == 3);
  REQUIRE(BeadsEngine::rateFor(BeadsEngine::Digital) == 48000.0);
  REQUIRE(BeadsEngine::rateFor(BeadsEngine::VintageDigital) == 32000.0);
  REQUIRE(BeadsEngine::rateFor(BeadsEngine::CleanTape) == 48000.0);
  REQUIRE(BeadsEngine::rateFor(BeadsEngine::DirtyTape) == 24000.0);
  REQUIRE(BeadsEngine::mediaFor(BeadsEngine::Digital) == int(beads::Processor::MEDIA_DIGITAL));
  REQUIRE(BeadsEngine::mediaFor(BeadsEngine::VintageDigital) == int(beads::Processor::MEDIA_VINTAGE_DIGITAL));
  REQUIRE(BeadsEngine::mediaFor(BeadsEngine::CleanTape) == int(beads::Processor::MEDIA_CLEAN_TAPE));
  REQUIRE(BeadsEngine::mediaFor(BeadsEngine::DirtyTape) == int(beads::Processor::MEDIA_DIRTY_TAPE));
  // The module's own rate table agrees (processor.h:47-55, ENABLE_HARDWARE_24K, indexed by Media).
  const float table[4] = {24000.0f, 48000.0f, 32000.0f, 48000.0f};
  for (int q = 0; q < 4; ++q) {
    INFO("quality " << q);
    REQUIRE(double(table[BeadsEngine::mediaFor(BeadsEngine::Quality(q))]) == BeadsEngine::rateFor(BeadsEngine::Quality(q)));
  }

  REQUIRE(BeadsEngine::qualityIndex(option(0)) == 0);
  REQUIRE(BeadsEngine::qualityIndex(option(3)) == 3);
  REQUIRE(BeadsEngine::qualityIndex(1.0f) == 3);  // past the last option: the last
  REQUIRE(BeadsEngine::seedIndex(option(0)) == 0);
  REQUIRE(BeadsEngine::seedIndex(option(1)) == 1);
  REQUIRE(BeadsEngine::seedIndex(1.0f) == 1);

  char buf[32];
  const char* const qualities[4] = {"Digital", "Vintage digital", "Clean tape", "Dirty tape"};
  for (int q = 0; q < 4; ++q) {
    info.params[9].format(option(q), buf, sizeof(buf));
    REQUIRE(std::string(buf) == qualities[q]);
  }
  info.params[10].format(option(0), buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Free");
  info.params[10].format(option(1), buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Notes");
  info.params[8].format(0.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Off");
  info.params[8].format(0.5f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "On");
  info.params[3].format(1.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "+24.0 st");
  info.params[12].format(1.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "+100%");
  info.params[12].format(0.5f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "+0%");
}

TEST_CASE("Granular Delay pot laws follow the firmware") {
  REQUIRE(BeadsEngine::pitchSemitones(0.5f) == 0.0f);
  REQUIRE(BeadsEngine::pitchSemitones(1.0f) == 24.0f);
  REQUIRE(BeadsEngine::pitchSemitones(0.0f) == -24.0f);
  REQUIRE(BeadsEngine::attenuverter(0.5f) == 0.0f);
  REQUIRE_THAT(BeadsEngine::attenuverter(1.0f), WithinAbs(1.0f, 1e-6));
  REQUIRE_THAT(BeadsEngine::attenuverter(0.0f), WithinAbs(-1.0f, 1e-6));
  REQUIRE(BeadsEngine::attenuverter(0.52f) == 0.0f);  // the dead zone around 50 %
  for (int i = 0; i <= 100; ++i) {
    const float v = float(i) / 100.0f;
    INFO("pot " << v);
    REQUIRE(BeadsEngine::pitchSemitones(v) == detuneLaw(v));
    REQUIRE(BeadsEngine::attenuverter(v) == attenuverterLaw(v));
  }
  // The DETUNE law's plateaus: a fifth and an octave up.
  REQUIRE(BeadsEngine::pitchSemitones(0.5f + 4.1f / 12.0f) == 7.0f);
  REQUIRE(BeadsEngine::pitchSemitones(0.5f + 5.1f / 12.0f) == 12.0f);
}

TEST_CASE("Granular Delay matches bare Beads at each Quality's own rate") {
  const int quality = GENERATE(0, 1, 2, 3);
  const auto mode = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  const double rate = kRates[quality];
  ParamValues p = defaults();
  p.p[9] = option(quality);
  p.p[1] = 0.2f;   // Time: grains from the recent past, which the noise has filled
  p.p[6] = 0.25f;  // Feedback
  p.p[7] = 0.5f;   // Reverb
  p.p[12] = 0.8f;  // the four Random amounts, both ways
  p.p[13] = 0.3f;
  p.p[14] = 0.7f;
  p.p[15] = 0.1f;
  const auto in = noise(size_t(rate * 3));
  BeadsEngine engine;
  engine.setConverterQuality(mode);
  engine.prepare(rate, 256);
  // At its own rate a Quality's path is bypassed: its one-block FIFO (preflight ruling G8) plus
  // the padding to the slowest path (ruling G20).
  const size_t latency = size_t(engine.nativeLatency());
  INFO("quality " << quality << " mode " << int(mode) << " compared at offset " << latency);
  stmlib::Random::Seed(1);
  const Stereo out = run(engine, in, 256, p);
  REQUIRE(engine.quality() == BeadsEngine::Quality(quality));
  Reference reference(quality);
  stmlib::Random::Seed(1);
  const Stereo ref = reference.run(
      in, [&](size_t) { return p; }, [](size_t) { return false; });
  REQUIRE(anyNonZero(ref.l, size_t(rate)));
  requireDelayedEqual(out, ref, latency);
}

TEST_CASE("Granular Delay follows moving controls and seeds like bare Beads") {
  // Every continuous control and Freeze move every 0.25 s; Seed = Notes with a note every 0.125 s.
  const int quality = GENERATE(0, 1, 2, 3);
  const double rate = kRates[quality];
  const size_t quarter = size_t(rate / 4);
  auto params = [&](size_t t) {
    ParamValues p = test::defaultParams(BeadsEngine::staticInfo());
    const size_t phase = t / quarter;
    for (int k : {0, 1, 2, 3, 4, 5, 6, 7, 12, 13, 14, 15}) p.p[k] = float((phase + size_t(k)) % 5) / 4.0f;
    p.p[8] = phase % 7 == 3 ? 1.0f : 0.0f;
    p.p[9] = option(quality);
    p.p[10] = option(1);
    return p;
  };
  auto seeded = [&](size_t t) { return t % (quarter / 2) == 0 && t > 0; };
  const auto in = noise(size_t(rate * 3), 7);
  BeadsEngine engine;
  engine.prepare(rate, 256);
  const int block = int(rate) / 4000;
  stmlib::Random::Seed(1);
  const Stereo out = run(engine, in, block, params, [&](size_t t) {
    if (seeded(t)) engine.noteOn(60.0f, 1.0f);
  });
  Reference reference(quality);
  stmlib::Random::Seed(1);
  const Stereo ref = reference.run(in, params, [&](size_t t) {
    for (size_t i = t; i < t + size_t(block); ++i)
      if (seeded(i)) return true;
    return false;
  });
  INFO("quality " << quality << " compared at offset " << engine.nativeLatency());
  requireDelayedEqual(out, ref, size_t(engine.nativeLatency()));
}

TEST_CASE("Seed Notes turns note-ons into SEED gates") {
  // Density 50 %: manual taps (trigger_generator.cc:287-310), so grains start only at SEED.
  const double rate = GENERATE(44100.0, 48000.0, 96000.0);
  const auto mode = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  const int seed = GENERATE(0, 1);
  INFO("rate " << rate << " mode " << int(mode) << " seed " << seed);
  ParamValues p = defaults();
  p.p[0] = 1.0f;  // Dry/Wet: wet only
  p.p[1] = 0.1f;  // Time: grains from the last 0.4 s, which the input has filled by the note
  p.p[4] = 0.5f;  // Density
  p.p[10] = option(seed);
  const auto in = noise(size_t(rate * 2), 3);
  BeadsEngine engine;
  engine.setConverterQuality(mode);
  engine.prepare(rate, 256);
  const size_t noteAt = (size_t(rate) + 15) / 16 * 16;  // a 16-sample call starts there
  stmlib::Random::Seed(1);
  const Stereo out = run(
      engine, in, 16, [&](size_t) { return p; },
      [&](size_t t) {
        if (t == noteAt) engine.noteOn(60.0f, 1.0f);
      });
  size_t first = 0;
  while (first < out.l.size() && std::fabs(out.l[first]) < 0.01f) ++first;  // past the converters' pre-ringing
  const double before = maxAbs(out.l, 0, noteAt);
  const double after = maxAbs(out.l, noteAt, out.l.size());
  INFO("before " << before << " after " << after << "; first sound " << int(first) - int(noteAt)
                 << " samples after the note, latency " << engine.nativeLatency());
  REQUIRE(before == 0.0);
  if (seed == BeadsEngine::SeedNotes) {
    REQUIRE(after > 0.01);
    // Aligned with the input: the grain starts with the audio that was playing when the note
    // came, which leaves the engine at its latency (the grain's envelope then rises within 1 ms).
    REQUIRE(first >= noteAt + size_t(engine.nativeLatency()) - 2);
    REQUIRE(first <= noteAt + size_t(engine.nativeLatency()) + size_t(std::ceil(rate / 1000.0)));
  } else {
    REQUIRE(after == 0.0);
  }
}

TEST_CASE("Seed Free: Density alone makes grains, none in its dead zone") {
  // The SEED button latched with nothing patched, as the module starts up (ui.cc:78): below 48 %
  // regular grains, above 52 % random ones, none in between (trigger_generator.cc:255-286).
  const float density = GENERATE(0.25f, 0.5f, 0.75f);
  INFO("density " << density);
  ParamValues p = defaults();
  p.p[0] = 1.0f;  // wet only
  p.p[1] = 0.1f;  // Time: the last 0.4 s
  p.p[4] = density;
  const auto in = noise(48000 * 2, 3);
  BeadsEngine engine;
  engine.prepare(48000.0, 256);
  stmlib::Random::Seed(1);
  const Stereo out = run(engine, in, 256, p);
  const double wet = maxAbs(out.l, 48000, out.l.size());
  INFO("wet " << wet);
  if (density == 0.5f) {
    REQUIRE(wet == 0.0);
  } else {
    REQUIRE(wet > 0.01);
  }
}

TEST_CASE("Dirty tape soft-clips its input like the manual-gain path") {
  const float soft = (1.0f - 0.05f) * (stmlib::SoftClip(1.5f + 0.05f) - stmlib::SoftClip(0.05f));
  REQUIRE(BeadsEngine::inputStage(1.5f, BeadsEngine::DirtyTape) == soft);
  REQUIRE(soft < 1.0f);
  // The other Qualities clamp hard at +-1 (Clean tape too: preflight ruling G19).
  for (auto q : {BeadsEngine::Digital, BeadsEngine::VintageDigital, BeadsEngine::CleanTape}) {
    REQUIRE(BeadsEngine::inputStage(1.5f, q) == 1.0f);
    REQUIRE(BeadsEngine::inputStage(-1.5f, q) == -1.0f);
    REQUIRE(BeadsEngine::inputStage(0.25f, q) == 0.25f);
    REQUIRE(BeadsEngine::inputStage(std::nanf(""), q) == 0.0f);
  }
  REQUIRE(BeadsEngine::inputStage(-4.0f, BeadsEngine::DirtyTape) == (1.0f - 0.05f) * (-1.0f - stmlib::SoftClip(0.05f)));
  // Silence in is silence through (the guard of the M5 stress test: no offset left over).
  REQUIRE(BeadsEngine::inputStage(0.0f, BeadsEngine::DirtyTape) == 0.0f);
  REQUIRE(BeadsEngine::inputStage(std::nanf(""), BeadsEngine::DirtyTape) == 0.0f);

  // In the engine: a hot input at Dirty tape's own rate reaches the processor soft-clipped, as
  // the restated reference does.
  ParamValues p = defaults();
  p.p[9] = option(BeadsEngine::DirtyTape);
  std::vector<float> in(24000);
  for (size_t i = 0; i < in.size(); ++i) in[i] = 1.5f * float(std::sin(2.0 * 3.14159265358979 * 200.0 * double(i) / 24000.0));
  BeadsEngine engine;
  engine.prepare(24000.0, 256);
  stmlib::Random::Seed(1);
  const Stereo out = run(engine, in, 256, p);
  Reference reference(BeadsEngine::DirtyTape);
  stmlib::Random::Seed(1);
  const Stereo ref = reference.run(
      in, [&](size_t) { return p; }, [](size_t) { return false; });
  requireDelayedEqual(out, ref, size_t(engine.nativeLatency()));
}

TEST_CASE("Quality changes fade and keep the latency") {
  const double rate = GENERATE(44100.0, 48000.0, 96000.0);
  const auto mode = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  INFO("rate " << rate << " mode " << int(mode));
  BeadsEngine engine;
  engine.setConverterQuality(mode);
  engine.prepare(rate, 256);
  const size_t latency = size_t(engine.nativeLatency());
  const size_t fade = size_t(std::lround(0.01 * rate));
  INFO("latency " << latency << " fade " << fade);
  auto at = [&](double seconds) { return size_t(seconds * rate); };

  // Dry only (Dry/Wet 0), no feedback or reverb: the dry path shows the latency and the fade.
  // 0-1 s Digital, impulse at 0.25 s, a 1 kHz sine from 0.5 s; at 1 s Dirty tape (sine until 2 s,
  // impulse at 2.5 s); at 3 s Vintage digital (impulse 3.5 s); at 4 s Clean tape (impulse 4.5 s);
  // at 5 s Digital again (impulse 5.5 s).
  const size_t n = at(6.0);
  std::vector<float> in(n, 0.0f);
  for (size_t i = at(0.5); i < at(2.0); ++i)
    in[i] = 0.5f * float(std::sin(2.0 * 3.14159265358979 * 1000.0 * double(i - at(0.5)) / rate));
  const double inputStep = maxStep(in, at(0.5), at(2.0));
  for (double s : {0.25, 2.5, 3.5, 4.5, 5.5}) in[at(s)] = 0.5f;
  const int sequence[6] = {0, BeadsEngine::DirtyTape, BeadsEngine::DirtyTape, BeadsEngine::VintageDigital,
                           BeadsEngine::CleanTape, BeadsEngine::Digital};
  auto params = [&](size_t t) {
    ParamValues p = defaults();
    p.p[0] = 0.0f;
    p.p[9] = option(sequence[std::min<size_t>(t / at(1.0), 5)]);
    return p;
  };
  stmlib::Random::Seed(1);
  const Stereo out = run(engine, in, 16, params, [](size_t) {});
  REQUIRE(allFinite(out.l));

  // The change at 1 s: within 2x the input's own largest step through 10 ms out, the silence while
  // the new path fills (the latency), and 10 ms in.
  const size_t change = (at(1.0) + 15) / 16 * 16;  // the first 16-sample call at or after 1 s
  const size_t end = change + 2 * fade + latency + fade;
  const double step = std::max(maxStep(out.l, change - 16, end), maxStep(out.r, change - 16, end));
  INFO("largest step " << step << " input's " << inputStep);
  REQUIRE(step <= 2.0 * inputStep);
  REQUIRE(maxAbs(out.l, change - fade, change) > 0.2);       // the old Quality fading out
  REQUIRE(maxAbs(out.l, change + fade, change + fade + latency) == 0.0);  // silent while the new path fills
  REQUIRE(maxAbs(out.l, change + 2 * fade + latency, at(2.0)) > 0.2);  // back after 10 ms in

  // Impulses: at the latency on every path. Dirty tape's wow and flutter is a delay of 1 to 123
  // samples at 24 kHz on top of it (beads/dsp/fx/wow_and_flutter.h:85), so there it is a window.
  auto peakAfter = [&](double seconds) {
    const size_t from = at(seconds);
    const std::vector<float> window(out.l.begin() + long(from), out.l.begin() + long(from + at(0.25)));
    return test::peakIndex(window);
  };
  const int digitalBefore = peakAfter(0.25);
  const int dirty = peakAfter(2.5);
  const int vintage = peakAfter(3.5);
  const int clean = peakAfter(4.5);
  const int digitalAfter = peakAfter(5.5);
  INFO("peaks: Digital " << digitalBefore << ", Dirty tape " << dirty << ", Vintage digital " << vintage
                         << ", Clean tape " << clean << ", Digital " << digitalAfter);
  REQUIRE(std::abs(digitalBefore - int(latency)) <= 1);
  REQUIRE(dirty >= int(latency) - 1);
  REQUIRE(dirty <= int(latency) + int(std::ceil(124.0 * rate / 24000.0)) + 1);
  REQUIRE(std::abs(vintage - int(latency)) <= 1);
  REQUIRE(std::abs(clean - int(latency)) <= 1);
  REQUIRE(std::abs(digitalAfter - int(latency)) <= 1);
  REQUIRE(engine.quality() == BeadsEngine::Digital);
  REQUIRE(engine.nativeLatency() == int(latency));
}

TEST_CASE("Granular Delay reset after 2 s of audio equals a fresh engine") {
  const double rate = 44100.0;  // every path converting or bypassed: 48 kHz and 24/32 kHz
  ParamValues p = defaults();
  p.p[6] = 0.5f;
  p.p[7] = 0.5f;
  const auto in = noise(size_t(rate * 2), 11);
  BeadsEngine used;
  used.prepare(rate, 256);
  ParamValues other = p;
  other.p[9] = option(BeadsEngine::DirtyTape);  // played at another Quality, frozen, seeded
  other.p[8] = 1.0f;
  other.p[10] = option(1);
  stmlib::Random::Seed(5);
  run(used, in, 256, p);
  run(used, in, 256, other);
  used.noteOn(60.0f, 1.0f);  // a pending seed is dropped too
  used.reset();
  BeadsEngine fresh;
  fresh.prepare(rate, 256);
  const auto next = noise(size_t(rate), 5);
  stmlib::Random::Seed(1);
  const Stereo a = run(used, next, 256, p);
  stmlib::Random::Seed(1);
  const Stereo b = run(fresh, next, 256, p);
  REQUIRE(anyNonZero(b.l));
  REQUIRE(a.l == b.l);
  REQUIRE(a.r == b.r);
}

TEST_CASE("Every Quality through the wrapper at 44.1, 48 and 96 kHz with odd blocks") {
  const auto mode = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  const double rate = GENERATE(44100.0, 48000.0, 96000.0);
  const int quality = GENERATE(0, 1, 2, 3);
  INFO("rate " << rate << " mode " << int(mode) << " quality " << quality);
  // As the slot runs it: at the host rate, in whole 16-sample blocks (preflight rulings G8, G17).
  auto make = [&] {
    auto w = std::make_unique<NativeRateWrapper>(std::make_unique<BeadsEngine>(), 0.0, 16);
    w->setQuality(mode);
    w->setWholeBlocks(true);
    w->prepare(rate, 256);
    return w;
  };
  // Noise for half a second, then grains from the buffer and the reverb's tail: the same output
  // whatever the host slicing. The P's stay put (a P change reaches the engine at the block its
  // host call lands in, which depends on the slicing).
  const size_t n = size_t(rate * 1.5);
  auto in = noise(n, 9);
  std::fill(in.begin() + long(rate * 0.5), in.end(), 0.0f);
  ParamValues p = defaults();
  p.p[6] = 0.5f;
  p.p[7] = 0.5f;
  p.p[9] = option(quality);
  auto events = [](size_t) {};
  auto params = [&](size_t) { return p; };
  auto a = make();
  auto b = make();
  auto c = make();
  stmlib::Random::Seed(1);
  const Stereo ra = run(*a, in, 1, params, events);
  stmlib::Random::Seed(1);
  const Stereo rb = run(*b, in, 7, params, events);
  stmlib::Random::Seed(1);
  const Stereo rc = run(*c, in, 129, params, events);
  REQUIRE(allFinite(ra.l));
  REQUIRE(allFinite(ra.r));
  REQUIRE(anyNonZero(ra.l, size_t(rate)));  // grains and reverb play on after the input stops
  REQUIRE(ra.l == rb.l);
  REQUIRE(ra.r == rb.r);
  REQUIRE(ra.l == rc.l);
  REQUIRE(ra.r == rc.r);

  // The converter quality reaches the inner wrappers: Live's converters are shorter.
  BeadsEngine live;
  live.setConverterQuality(ConverterQuality::Live);
  live.prepare(rate, 256);
  BeadsEngine studio;
  studio.setConverterQuality(ConverterQuality::Studio);
  studio.prepare(rate, 256);
  INFO("latency Live " << live.nativeLatency() << " Studio " << studio.nativeLatency());
  REQUIRE(live.nativeLatency() < studio.nativeLatency());
  REQUIRE(a->latencySamples() == (mode == ConverterQuality::Live ? live : studio).nativeLatency() + 16);
}

TEST_CASE("Granular Delay allocates nothing in process, including Quality changes") {
  BeadsEngine engine;
  engine.prepare(44100.0, 256);
  const auto in = noise(256, 4);
  std::vector<float> l(256), r(256);
  const float* i2[2] = {in.data(), in.data()};
  float* out[2] = {l.data(), r.data()};
  ParamValues p = defaults();
  engine.process(i2, out, p, 256);
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int block = 0; block < 1200; ++block) {
      for (int k : {0, 1, 2, 3, 4, 5, 6, 7, 12, 13, 14, 15}) p.p[k] = float((block + k) % 7) / 6.0f;
      p.p[8] = block % 20 < 10 ? 0.0f : 1.0f;
      p.p[9] = option((block / 50) % 4);
      p.p[10] = option((block / 100) % 2);
      if (block % 30 == 0) engine.noteOn(60.0f, 1.0f);
      if (block == 600) engine.reset();
      engine.process(block % 3 == 0 ? nullptr : i2, out, p, 1 + block % 256);
    }
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}
