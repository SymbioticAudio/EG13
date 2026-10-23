// Converter quality: the Live / Studio Latency Mode (M5 plan Task 2; owner decision 3; rulings 1-6;
// pre-flight rulings G1-G4). Studio is the converter EG13 shipped; Live is the lowest-latency
// r8brain converter that meets the quality floors below, chosen by the "[.converter]" report.

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "CDSPResampler.h"
#include "engine/engine_registry.h"
#include "engine/native_rate_wrapper.h"
#include "engine/plaits_engine.h"
#include "params/param_ids.h"
#include "state/state.h"
#include "synth/param_table.h"
#include "synth/synth.h"
#include "synth_helpers.h"
#include "test_engines.h"
#include "test_helpers.h"
#include "voice/processor_factory.h"
#include "voice/processor_slot.h"
#include "voice/voice.h"

using namespace eg13;

namespace {

// Ruling 1's floors: the owner's provisional quality floors, as two constants.
constexpr double kStopBandFloorDb = 96.0;
constexpr double kRippleFloorDb = 0.1;

constexpr double kTwoPi = 6.283185307179586;
constexpr double kAmplitude = 0.5;
constexpr size_t kAnalysis = 16384;  // samples analysed per probe, after settling

const double kHosts[] = {44100.0, 48000.0, 96000.0};
// Every converted native rate in the synth (ruling 1), with a block it runs in.
struct Native {
  double rate;
  int block;
};
const Native kNatives[] = {{24000.0, 6}, {32000.0, 16}, {47872.34, 12}, {48000.0, 24}, {96000.0, 60}};
// Every (rate, block) a converter runs with, today and in the M5 Part FX: the latency and the
// input prime depend on the block.
const Native kEngineRates[] = {{24000.0, 6},  {32000.0, 8},  {32000.0, 16}, {32000.0, 32}, {47872.34, 12},
                               {48000.0, 12}, {48000.0, 24}, {96000.0, 24}, {96000.0, 60}};

bool bypassed(double host, double native) { return std::fabs(host - native) < 1e-6; }

const char* name(ConverterQuality q) { return q == ConverterQuality::Live ? "Live" : "Studio"; }

// Records its native-rate input (channel 0); outputs silence.
class CaptureEngine final : public VoiceEngine {
 public:
  explicit CaptureEngine(std::vector<float>* sink) : sink_(sink) {}
  void prepare(double, int) override {}
  void reset() override { sink_->clear(); }
  void noteOn(float, float) override {}
  void noteOff() override {}
  void setNote(float) override {}
  void process(const float* const* in, float* const* out, const ParamValues&, int n) override {
    for (int i = 0; i < n; ++i) {
      sink_->push_back(in != nullptr ? in[0][i] : 0.0f);
      out[0][i] = out[1][i] = 0.0f;
    }
  }
  const EngineInfo& info() const override {
    static const EngineInfo kInfo = {"capture", "Capture", 0, nullptr, true};
    return kInfo;
  }

 private:
  std::vector<float>* sink_;
};

// A kAmplitude sine at a set frequency, at the native rate.
class ToneEngine final : public VoiceEngine {
 public:
  void prepare(double rate, int) override { rate_ = rate; }
  void reset() override { t_ = 0; }
  void noteOn(float, float) override {}
  void noteOff() override {}
  void setNote(float) override {}
  void process(const float* const*, float* const* out, const ParamValues&, int n) override {
    for (int i = 0; i < n; ++i, ++t_) {
      out[0][i] = out[1][i] = float(kAmplitude * std::sin(kTwoPi * frequency * double(t_) / rate_));
    }
  }
  const EngineInfo& info() const override {
    static const EngineInfo kInfo = {"tone", "Tone", 0, nullptr, false};
    return kInfo;
  }
  double frequency = 1000.0;

 private:
  double rate_ = 48000.0;
  long long t_ = 0;
};

// Least-squares fit of a sine at f: its amplitude, and the amplitude (sqrt 2 x RMS) of what is
// left, which holds every other component (images, aliases, spurs).
struct Fit {
  double amplitude;
  double rest;
};

Fit fitSine(const float* x, size_t n, double f, double rate) {
  double cc = 0, ss = 0, cs = 0, xc = 0, xs = 0;
  const double w = kTwoPi * f / rate;
  for (size_t i = 0; i < n; ++i) {
    const double c = std::cos(w * double(i)), s = std::sin(w * double(i));
    cc += c * c;
    ss += s * s;
    cs += c * s;
    xc += double(x[i]) * c;
    xs += double(x[i]) * s;
  }
  const double det = cc * ss - cs * cs;
  const double a = (xc * ss - xs * cs) / det;
  const double b = (xs * cc - xc * cs) / det;
  double rest = 0.0;
  for (size_t i = 0; i < n; ++i) {
    const double r = double(x[i]) - a * std::cos(w * double(i)) - b * std::sin(w * double(i));
    rest += r * r;
  }
  return {std::sqrt(a * a + b * b), std::sqrt(2.0 * rest / double(n))};
}

double amplitudeOf(const float* x, size_t n) {
  double sum = 0.0;
  for (size_t i = 0; i < n; ++i) sum += double(x[i]) * double(x[i]);
  return std::sqrt(2.0 * sum / double(n));
}

double db(double ratio) { return 20.0 * std::log10(std::max(ratio, 1e-30)); }

// One converter direction of a wrapper, from -> to: a kAmplitude sine at f (rate `from`) in, the
// settled kAnalysis samples at rate `to` out.
class Direction {
 public:
  // input: the wrapper's input path (host -> native); else its output path (native -> host).
  Direction(const ConverterSpec& spec, double host, const Native& native, bool input) : input_(input) {
    from_ = input ? host : native.rate;
    to_ = input ? native.rate : host;
    std::unique_ptr<VoiceEngine> engine;
    if (input) {
      engine = std::make_unique<CaptureEngine>(&captured_);
    } else {
      auto tone = std::make_unique<ToneEngine>();
      tone_ = tone.get();
      engine = std::move(tone);
    }
    wrapper_ = std::make_unique<NativeRateWrapper>(std::move(engine), native.rate, native.block);
    wrapper_->setSpecForTest(spec);
    wrapper_->prepare(host, kBlock);
    const double latency = input ? double(wrapper_->inputPathLatencySamples()) * native.rate / host
                                 : double(wrapper_->latencySamples());
    settle_ = size_t(4096 + 2 * latency);
    captured_.reserve(settle_ + kAnalysis + 4096);
    buffer_.assign(kBlock, 0.0f);
    left_.assign(kBlock, 0.0f);
    right_.assign(kBlock, 0.0f);
  }
  double from() const { return from_; }
  double to() const { return to_; }

  std::vector<float> run(double f) {
    wrapper_->reset();
    const size_t want = settle_ + kAnalysis;
    std::vector<float> out;
    if (input_) {
      long long t = 0;
      while (captured_.size() < want) {
        for (int i = 0; i < kBlock; ++i, ++t) {
          buffer_[size_t(i)] = float(kAmplitude * std::sin(kTwoPi * f * double(t) / from_));
        }
        const float* in[2] = {buffer_.data(), buffer_.data()};
        float* o[2] = {left_.data(), right_.data()};
        wrapper_->process(in, o, ParamValues{}, kBlock);
      }
      out.assign(captured_.begin() + long(settle_), captured_.begin() + long(want));
    } else {
      tone_->frequency = f;
      std::vector<float> all(want);
      for (size_t done = 0; done < want;) {
        const int n = int(std::min<size_t>(kBlock, want - done));
        float* o[2] = {all.data() + done, right_.data()};
        wrapper_->process(nullptr, o, ParamValues{}, n);
        done += size_t(n);
      }
      out.assign(all.begin() + long(settle_), all.end());
    }
    return out;
  }

 private:
  static constexpr int kBlock = 256;
  bool input_;
  double from_ = 0.0, to_ = 0.0;
  std::vector<float> captured_;
  ToneEngine* tone_ = nullptr;
  std::unique_ptr<NativeRateWrapper> wrapper_;
  size_t settle_ = 0;
  std::vector<float> buffer_, left_, right_;
};

// G3: a down-converter from -> to aliases a sine in [to/2, from/2) at -96 dB or less; an
// up-converter's images (everything but the sine) are at -96 dB or less, for every sine below
// from/2. Relative to the same sine at 1 kHz. Probes every 250 Hz.
double directionRejectionDb(Direction& d) {
  const auto ref = d.run(1000.0);
  const double reference = fitSine(ref.data(), ref.size(), 1000.0, d.to()).amplitude;
  double worst = 0.0;
  if (d.from() > d.to()) {
    for (double f = d.to() / 2.0; f < d.from() / 2.0; f += 250.0) {
      const auto out = d.run(f);
      worst = std::max(worst, amplitudeOf(out.data(), out.size()));
    }
  } else {
    for (double f = 250.0; f < d.from() / 2.0; f += 250.0) {
      const auto out = d.run(f);
      worst = std::max(worst, fitSine(out.data(), out.size(), f, d.to()).rest);
    }
  }
  return db(reference / worst);
}

// The worst of both paths of a wrapper at host <-> native (ruling 1 (a), G3).
double stopBandRejectionDb(const ConverterSpec& spec, double host, const Native& native) {
  Direction in(spec, host, native, true);
  Direction out(spec, host, native, false);
  return std::min(directionRejectionDb(in), directionRejectionDb(out));
}

// The round trip host -> native -> host (an echo engine): a kAmplitude sine's level in dB.
class RoundTrip {
 public:
  RoundTrip(const ConverterSpec& spec, double host, const Native& native) : host_(host) {
    wrapper_ = std::make_unique<NativeRateWrapper>(std::make_unique<test::EchoEngine>(), native.rate, native.block);
    wrapper_->setSpecForTest(spec);
    wrapper_->prepare(host, kBlock);
    latency_ = wrapper_->inputPathLatencySamples();
    settle_ = size_t(4096 + 2 * latency_);
  }
  int latency() const { return latency_; }
  // The settled output and the input, for a sine at f.
  void run(double f, std::vector<float>& in, std::vector<float>& out) {
    wrapper_->reset();
    const size_t n = settle_ + kAnalysis;
    in.resize(n);
    out.assign(n, 0.0f);
    std::vector<float> right(kBlock);
    for (size_t i = 0; i < n; ++i) in[i] = float(kAmplitude * std::sin(kTwoPi * f * double(i) / host_));
    for (size_t done = 0; done < n;) {
      const int count = int(std::min<size_t>(kBlock, n - done));
      const float* i2[2] = {in.data() + done, in.data() + done};
      float* o2[2] = {out.data() + done, right.data()};
      wrapper_->process(i2, o2, ParamValues{}, count);
      done += size_t(count);
    }
  }
  double levelDb(double f) {
    std::vector<float> in, out;
    run(f, in, out);
    return db(fitSine(out.data() + settle_, kAnalysis, f, host_).amplitude / kAmplitude);
  }
  // Dry (the input delayed by the reported input-path latency) plus the round trip, halved.
  double dryPlusWetDb(double f) {
    std::vector<float> in, out;
    run(f, in, out);
    std::vector<float> sum(kAnalysis);
    for (size_t i = 0; i < kAnalysis; ++i) {
      const size_t at = settle_ + i;
      sum[i] = 0.5f * (out[at] + in[at - size_t(latency_)]);
    }
    return db(fitSine(sum.data(), kAnalysis, f, host_).amplitude / kAmplitude);
  }
  size_t settle() const { return settle_; }

 private:
  static constexpr int kBlock = 256;
  double host_;
  std::unique_ptr<NativeRateWrapper> wrapper_;
  int latency_ = 0;
  size_t settle_ = 0;
};

// Ruling 1 (b): f_pass, where the Studio round trip at these rates first falls 0.1 dB below its
// 1 kHz level (100 Hz steps). Cached.
double passbandEdge(double host, const Native& native) {
  static std::map<std::pair<double, double>, double> cache;
  const auto key = std::make_pair(host, native.rate);
  const auto found = cache.find(key);
  if (found != cache.end()) return found->second;
  RoundTrip studio(converterSpec(ConverterQuality::Studio), host, native);
  const double ref = studio.levelDb(1000.0);
  double f = 1100.0;
  while (f < host / 2.0 && studio.levelDb(f) >= ref - kRippleFloorDb) f += 100.0;
  cache[key] = f;
  return f;
}

// Ruling 1 (b): the largest deviation of the round trip from its 1 kHz level, 20 Hz to f_pass
// (every 100 Hz).
double passbandRippleDb(const ConverterSpec& spec, double host, const Native& native) {
  const double edge = passbandEdge(host, native);
  RoundTrip trip(spec, host, native);
  const double ref = trip.levelDb(1000.0);
  double worst = std::fabs(trip.levelDb(20.0) - ref);
  for (double f = 100.0; f < edge; f += 100.0) worst = std::max(worst, std::fabs(trip.levelDb(f) - ref));
  return worst;
}

int inputPathLatency(const ConverterSpec& spec, double host, const Native& native) {
  NativeRateWrapper w(std::make_unique<test::EchoEngine>(), native.rate, native.block);
  w.setSpecForTest(spec);
  w.prepare(host, 512);
  return w.inputPathLatencySamples();
}

int outputPathLatency(const ConverterSpec& spec, double host, const Native& native) {
  NativeRateWrapper w(std::make_unique<test::ImpulseEngine>(), native.rate, native.block);
  w.setSpecForTest(spec);
  w.prepare(host, 512);
  return w.latencySamples();
}

// The input FIFO never runs dry or full: host blocks of 1, 7, 129 and maxBlock samples (and a
// mixed pattern), maxBlock 512 (Sources) and 16 (the voice's Filter and Shaper).
bool primeHolds(const ConverterSpec& spec, double host, const Native& native, double seconds) {
  for (int maxBlock : {16, 512}) {
    std::vector<int> blocks = {1, 7, maxBlock, -1};
    if (maxBlock > 129) blocks.push_back(129);
    for (int block : blocks) {
      NativeRateWrapper w(std::make_unique<test::EchoEngine>(), native.rate, native.block);
      w.setSpecForTest(spec);
      w.prepare(host, maxBlock);
      const size_t size = static_cast<size_t>(maxBlock);
      std::vector<float> in(size), l(size), r(size);
      const float* i2[2] = {in.data(), in.data()};
      float* o2[2] = {l.data(), r.data()};
      static const int kPattern[] = {1, 129, 7, 512, 64, 3, 255, 16};
      const long long total = (long long)(seconds * host);
      long long t = 0;
      for (int call = 0; t < total; ++call) {
        const int n = block > 0 ? block : std::min(maxBlock, kPattern[call % 8]);
        for (int i = 0; i < n; ++i) in[size_t(i)] = float(0.5 * std::sin(0.05 * double(t + i)));
        w.process(i2, o2, ParamValues{}, n);
        t += n;
      }
      if (w.inputUnderruns() != 0 || w.inputOverruns() != 0) return false;
    }
  }
  return true;
}

// Ruling 3 (for minimum phase): an impulse lands at the reported latency +-1 on both paths, and Dry
// plus a unity engine path has no comb notch deeper than 1 dB below f_pass.
struct Alignment {
  int worstOutputError = 0;
  int worstInputError = 0;
  double deepestNotchDb = 0.0;
};

Alignment alignment(const ConverterSpec& spec, double host, const Native& native) {
  Alignment a;
  {
    NativeRateWrapper w(std::make_unique<test::ImpulseEngine>(), native.rate, native.block);
    w.setSpecForTest(spec);
    w.prepare(host, 256);
    w.noteOn(60.0f, 1.0f);
    std::vector<float> l(8000), r(8000);
    for (size_t done = 0; done < l.size(); done += 100) {
      float* o[2] = {l.data() + done, r.data() + done};
      w.process(nullptr, o, ParamValues{}, 100);
    }
    a.worstOutputError = std::abs(test::peakIndex(l) - w.latencySamples());
  }
  {
    NativeRateWrapper w(std::make_unique<test::EchoEngine>(), native.rate, native.block);
    w.setSpecForTest(spec);
    w.prepare(host, 256);
    std::vector<float> in(12000, 0.0f), l(12000), r(12000);
    in[3000] = 1.0f;
    for (size_t done = 0; done < in.size(); done += 100) {
      const float* i2[2] = {in.data() + done, in.data() + done};
      float* o[2] = {l.data() + done, r.data() + done};
      w.process(i2, o, ParamValues{}, 100);
    }
    a.worstInputError = std::abs(test::peakIndex(l) - 3000 - w.inputPathLatencySamples());
  }
  RoundTrip trip(spec, host, native);
  const double edge = passbandEdge(host, native);
  for (double f = 100.0; f < edge; f += 100.0) a.deepestNotchDb = std::min(a.deepestNotchDb, trip.dryPlusWetDb(f));
  return a;
}

struct FloorResult {
  double rejectionDb = 1e9;
  double rippleDb = 0.0;
};

FloorResult floors(const ConverterSpec& spec) {
  FloorResult r;
  for (double host : kHosts) {
    for (const Native& native : kNatives) {
      if (bypassed(host, native.rate)) continue;
      r.rejectionDb = std::min(r.rejectionDb, stopBandRejectionDb(spec, host, native));
      r.rippleDb = std::max(r.rippleDb, passbandRippleDb(spec, host, native));
    }
  }
  return r;
}

uint32_t masterId(MasterParam m) { return paramSpec(masterParamIndex(m)).id; }

}  // namespace

TEST_CASE("Studio is today's converter bit for bit") {
  const double host = 44100.0;
  const auto p = test::defaultParams(PlaitsEngine::staticInfo());
  NativeRateWrapper wrapped(std::make_unique<PlaitsEngine>(), PlaitsEngine::kNativeRate, 12);
  wrapped.setQuality(ConverterQuality::Studio);
  wrapped.prepare(host, 512);
  wrapped.noteOn(60.0f, 1.0f);
  const int n = 20000;
  std::vector<float> l(n), r(n);
  for (int done = 0; done < n; done += 512) {
    float* o[2] = {l.data() + done, r.data() + done};
    wrapped.process(nullptr, o, p, std::min(512, n - done));
  }
  // By hand: the engine at its native rate in native blocks, through r8brain's 24-bit converter.
  PlaitsEngine direct;
  direct.prepare(PlaitsEngine::kNativeRate, 12);
  direct.noteOn(60.0f, 1.0f);
  r8b::CDSPResampler24 converter(PlaitsEngine::kNativeRate, host, 12, 30.0);
  std::vector<float> expected(size_t(wrapped.latencySamples()), 0.0f);
  float a[12], b[12];
  double x[12];
  while (expected.size() < size_t(n)) {
    float* o[2] = {a, b};
    direct.process(nullptr, o, p, 12);
    for (int i = 0; i < 12; ++i) x[i] = double(a[i]);
    double* y = nullptr;
    const int made = converter.process(x, 12, y);
    for (int i = 0; i < made; ++i) expected.push_back(float(y[i]));
  }
  expected.resize(size_t(n));
  REQUIRE(l == expected);
}

TEST_CASE("Live converters meet the floors at every rate pair") {
  for (double host : kHosts) {
    for (const Native& native : kNatives) {
      if (bypassed(host, native.rate)) continue;
      const ConverterSpec live = converterSpec(ConverterQuality::Live);
      const double rejection = stopBandRejectionDb(live, host, native);
      const double ripple = passbandRippleDb(live, host, native);
      INFO("host " << host << " native " << native.rate << " rejection " << rejection << " dB ripple " << ripple
                   << " dB (f_pass " << passbandEdge(host, native) << " Hz)");
      REQUIRE(rejection >= kStopBandFloorDb);  // input aliases and output images, ruling 1, G3
      REQUIRE(ripple <= kRippleFloorDb);       // 20 Hz .. f_pass of Studio at the same rates
    }
  }
}

TEST_CASE("Studio meets the same floors") {
  for (double host : kHosts) {
    for (const Native& native : kNatives) {
      if (bypassed(host, native.rate)) continue;
      const ConverterSpec studio = converterSpec(ConverterQuality::Studio);
      const double rejection = stopBandRejectionDb(studio, host, native);
      const double ripple = passbandRippleDb(studio, host, native);
      INFO("host " << host << " native " << native.rate << " rejection " << rejection << " dB ripple " << ripple);
      REQUIRE(rejection >= kStopBandFloorDb);
      REQUIRE(ripple <= kRippleFloorDb);
    }
  }
}

TEST_CASE("Live is not slower than Studio on any path") {
  const ConverterSpec live = converterSpec(ConverterQuality::Live);
  const ConverterSpec studio = converterSpec(ConverterQuality::Studio);
  for (double host : {44100.0, 48000.0, 88200.0, 96000.0, 192000.0}) {
    for (const Native& native : kEngineRates) {
      INFO("host " << host << " native " << native.rate << "/" << native.block);
      REQUIRE(inputPathLatency(live, host, native) <= inputPathLatency(studio, host, native));
      REQUIRE(outputPathLatency(live, host, native) <= outputPathLatency(studio, host, native));
    }
  }
}

TEST_CASE("Latency is constant within a mode") {
  const auto q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  // A slot of echo engines at the converted rates of the synth, and one at the host rate.
  std::vector<SourceSpec> engines;
  engines.push_back({std::make_unique<test::EchoEngine>(), 0.0, 16});
  for (const Native& native : kNatives) engines.push_back({std::make_unique<test::EchoEngine>(), native.rate, native.block});
  ProcessorSlot slot(std::move(engines));
  for (double host : kHosts) {
    slot.prepare(host, 129, q);
    const int latency = slot.latencySamples();
    for (int block : {1, 7, 129}) {
      for (int e = 0; e < slot.engineCount(); ++e) {
        slot.reset();
        slot.setEngine(e);  // nothing played since the reset: switches at once
        std::vector<float> in(8000, 0.0f), l(8000), r(8000);
        in[2000] = 1.0f;
        for (size_t done = 0; done < in.size();) {
          const int n = int(std::min<size_t>(size_t(block), in.size() - done));
          const float* i2[2] = {in.data() + done, in.data() + done};
          float* o2[2] = {l.data() + done, r.data() + done};
          slot.process(i2, nullptr, o2, ParamValues{}, n);
          done += size_t(n);
        }
        INFO(name(q) << " host " << host << " block " << block << " engine " << e << " latency " << latency
                     << " peak at " << test::peakIndex(l) - 2000);
        REQUIRE(std::abs(test::peakIndex(l) - 2000 - latency) <= 1);
      }
    }
  }
}

TEST_CASE("Onset alignment holds in both modes") {
  const auto q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  SECTION("Instrument: every Source x Shaper onset lands at the voice latency") {
    // The Sources and Shapers at their real native rates and blocks (an impulse for each Source, an
    // echo for each Shaper), so the padding of each slot is what the synth runs.
    auto sources = [] {
      std::vector<SourceSpec> s;
      for (int e = 0; e < kNumSourceEngines; ++e) {
        const SourceSpec real = makeSource(e);
        s.push_back({std::make_unique<test::ImpulseEngine>(), real.nativeRate, real.nativeBlock});
      }
      return s;
    };
    auto shapers = [] {
      std::vector<SourceSpec> s;
      for (int e = 0; e < kNumShaperEngines; ++e) {
        const SourceSpec real = makeShaper(e);
        s.push_back({std::make_unique<test::EchoEngine>(), real.nativeRate, real.nativeBlock});
      }
      return s;
    };
    for (double host : kHosts) {
      for (int source = 0; source < kNumSourceEngines; ++source) {
        for (int shaper = 0; shaper < kNumShaperEngines; ++shaper) {
          Voice voice(sources(), makeOffOnly(), shapers());
          voice.prepare(host, 256, Voice::Alignment::Notes, q);
          voice.setEngine(source);
          voice.setShaperEngine(shaper);
          voice.trigger(60, 1.0f);
          std::vector<float> l(8000, 0.0f), r(8000, 0.0f);
          for (size_t done = 0; done < l.size(); done += 256) {
            float* out[2] = {l.data() + done, r.data() + done};
            voice.process(out, ParamValues{}, 256);
          }
          INFO(name(q) << " host " << host << " source " << source << " shaper " << shaper << " latency "
                       << voice.latencySamples() << " onset " << test::peakIndex(l));
          REQUIRE(std::abs(test::peakIndex(l) - voice.latencySamples()) <= 1);
        }
      }
    }
  }
  SECTION("EG13 FX: the Audio Input path lands on Dry, sample-exact") {
    for (double host : kHosts) {
      Synth synth(PluginKind::Effect);
      test::m3Patch(synth);
      for (int part = 0; part < kNumParts; ++part) synth.setParam(paramSpec(filterEngineParamIndex(part)).id, 0);
      synth.setParam(masterId(MasterParam::Dry), 1.0);
      synth.setParam(masterId(MasterParam::LatencyMode), double(int(q)));
      synth.prepare(host, 256);
      REQUIRE(synth.preparedQuality() == q);
      const size_t latency = size_t(synth.latencySamples());
      const size_t n = 16000;
      std::vector<float> inL(n, 0.0f), outL(n), outR(n);
      inL[8000] = 0.5f;
      for (size_t done = 0; done < n; done += 256) {
        const int count = int(std::min<size_t>(256, n - done));
        const float* i2[2] = {inL.data() + done, inL.data() + done};
        float* o2[2] = {outL.data() + done, outR.data() + done};
        synth.process(i2, o2, nullptr, count);
      }
      INFO(name(q) << " host " << host << " latency " << latency);
      REQUIRE(std::fabs(outL[8000 + latency] - 1.0f) < 1e-3f);  // wet + dry on the same sample
      REQUIRE(std::fabs(outL[8000 + latency - 1]) < 1e-3f);
      REQUIRE(std::fabs(outL[8000 + latency + 1]) < 1e-3f);
    }
  }
}

TEST_CASE("The Live input prime never under- or overruns") {
  const ConverterSpec live = converterSpec(ConverterQuality::Live);
  for (double host : kHosts) {
    for (const Native& native : kEngineRates) {
      if (bypassed(host, native.rate)) continue;
      INFO("host " << host << " native " << native.rate << "/" << native.block);
      REQUIRE(primeHolds(live, host, native, 10.0));
    }
  }
}

TEST_CASE("A mode change waits for the next prepare") {
  for (PluginKind kind : {PluginKind::Instrument, PluginKind::Effect}) {
    Synth synth(kind);
    synth.prepare(48000.0, 512);
    synth.setActive(true);
    const ConverterQuality before = synth.preparedQuality();
    const int latency = synth.latencySamples();
    const ConverterQuality other = before == ConverterQuality::Live ? ConverterQuality::Studio : ConverterQuality::Live;
    synth.setParam(masterId(MasterParam::LatencyMode), double(int(other)));  // as the audio thread does
    std::vector<float> l(512), r(512);
    float* out[2] = {l.data(), r.data()};
    synth.process(nullptr, out, nullptr, 512);
    REQUIRE(synth.requestedQuality() == other);
    REQUIRE(synth.preparedQuality() == before);
    REQUIRE(synth.latencySamples() == latency);
    synth.setActive(false);
    synth.prepare(48000.0, 512);
    REQUIRE(synth.preparedQuality() == other);
    REQUIRE(synth.latencySamples() != latency);
  }
}

TEST_CASE("Reported latency matches the running mode after a state load") {
  for (PluginKind kind : {PluginKind::Instrument, PluginKind::Effect}) {
    Synth studio(kind);
    studio.setParam(masterId(MasterParam::LatencyMode), double(int(ConverterQuality::Studio)));
    studio.prepare(48000.0, 512);
    const int studioLatency = studio.latencySamples();
    Synth live(kind);
    live.setParam(masterId(MasterParam::LatencyMode), double(int(ConverterQuality::Live)));
    live.prepare(48000.0, 512);
    const int liveLatency = live.latencySamples();
    REQUIRE(liveLatency < studioLatency);
    const std::string saved = state::StateStore().save(studio);

    live.setActive(true);
    state::StateStore store;
    REQUIRE(store.load(live, saved) != state::LoadResult::Invalid);
    std::vector<float> l(512), r(512);
    float* out[2] = {l.data(), r.data()};
    live.process(nullptr, out, nullptr, 512);  // the audio thread takes the load
    REQUIRE(live.requestedQuality() == ConverterQuality::Studio);
    REQUIRE(live.preparedQuality() == ConverterQuality::Live);
    REQUIRE(live.latencySamples() == liveLatency);  // what is running
    live.setActive(false);
    live.prepare(48000.0, 512);  // the host's restart
    REQUIRE(live.preparedQuality() == ConverterQuality::Studio);
    REQUIRE(live.latencySamples() == studioLatency);
  }
}

TEST_CASE("Latency Mode defaults: instrument Live, EG13 FX Studio") {
  const size_t index = masterParamIndex(MasterParam::LatencyMode);
  REQUIRE(paramSpec(index).id == params::id(params::kMasterPart, params::Slot::Part, 6));
  REQUIRE(paramSpec(index).stepped);
  REQUIRE(paramSpec(index).minValue == 0.0f);
  REQUIRE(paramSpec(index).maxValue == 1.0f);
  REQUIRE(paramDefault(index, PluginKind::Instrument) == float(ConverterQuality::Live));
  REQUIRE(paramDefault(index, PluginKind::Effect) == float(ConverterQuality::Studio));
  char buf[32];
  formatParam(index, 0, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Live");
  formatParam(index, 1, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Studio");
  Synth instrument(PluginKind::Instrument);
  Synth fx(PluginKind::Effect);
  instrument.prepare(48000.0, 256);
  fx.prepare(48000.0, 256);
  REQUIRE(instrument.preparedQuality() == ConverterQuality::Live);
  REQUIRE(fx.preparedQuality() == ConverterQuality::Studio);
}

// Ruling 2's candidates with ruling 1's floors, ruling 3's alignment for minimum phase and the
// prime check; prints each with its latencies and the selection. Not run in CI (minutes).
TEST_CASE("Live candidate report", "[.converter]") {
  struct Candidate {
    ConverterSpec spec;
    bool floorsMet = false;
    bool primeOk = true;
    bool alignmentOk = true;
    int worst48 = 0;
  };
  std::vector<Candidate> candidates;
  for (ConverterPhase phase : {ConverterPhase::Linear, ConverterPhase::Minimum}) {
    for (double atten : {96.0, 108.0, 120.0, 136.0, 150.0, 180.15}) {
      for (int prime : {2, 1}) {
        candidates.push_back({{phase, atten, NativeRateWrapper::kTransitionBandPercent, prime}});
      }
    }
  }
  std::printf("Live candidates (transition band 30 %%). Floors: stop band >= %.0f dB, ripple <= %.2f dB.\n",
              kStopBandFloorDb, kRippleFloorDb);
  std::printf("%-8s %7s %5s | %9s %8s | %5s | %-34s | %s\n", "phase", "atten", "prime", "rej. dB", "ripple",
              "prime", "48 kHz input-path latency per native", "worst");
  std::map<std::pair<int, double>, FloorResult> floorCache;  // floors do not depend on the prime
  std::map<std::pair<int, double>, Alignment> alignCache;
  for (Candidate& c : candidates) {
    const auto key = std::make_pair(int(c.spec.phase), c.spec.attenuationDb);
    if (floorCache.find(key) == floorCache.end()) floorCache[key] = floors(c.spec);
    const FloorResult f = floorCache[key];
    c.floorsMet = f.rejectionDb >= kStopBandFloorDb && f.rippleDb <= kRippleFloorDb;
    if (c.spec.primeBlocks != 2) {
      for (double host : kHosts) {
        for (const Native& native : kEngineRates) {
          if (!bypassed(host, native.rate)) c.primeOk = c.primeOk && primeHolds(c.spec, host, native, 2.0);
        }
      }
    }
    std::string latencies;
    for (const Native& native : kEngineRates) {
      const int l = bypassed(48000.0, native.rate) ? 0 : inputPathLatency(c.spec, 48000.0, native);
      c.worst48 = std::max(c.worst48, l);
      latencies += std::to_string(l) + " ";
    }
    if (c.spec.phase == ConverterPhase::Minimum) {
      if (alignCache.find(key) == alignCache.end()) {
        Alignment worst;
        for (double host : kHosts) {
          for (const Native& native : kNatives) {
            if (bypassed(host, native.rate)) continue;
            const Alignment a = alignment(c.spec, host, native);
            worst.worstOutputError = std::max(worst.worstOutputError, a.worstOutputError);
            worst.worstInputError = std::max(worst.worstInputError, a.worstInputError);
            worst.deepestNotchDb = std::min(worst.deepestNotchDb, a.deepestNotchDb);
          }
        }
        alignCache[key] = worst;
      }
      const Alignment& a = alignCache[key];
      c.alignmentOk = a.worstOutputError <= 1 && a.worstInputError <= 1 && a.deepestNotchDb >= -1.0;
      std::printf("%-8s %7.2f %5d | %9.2f %8.4f | %5s | %-34s | %4d  (align: out %+d in %+d notch %.2f dB%s)\n",
                  "minimum", c.spec.attenuationDb, c.spec.primeBlocks, f.rejectionDb, f.rippleDb,
                  c.primeOk ? "ok" : "FAIL", latencies.c_str(), c.worst48, a.worstOutputError, a.worstInputError,
                  a.deepestNotchDb, c.alignmentOk ? "" : ", FAILS ruling 3");
    } else {
      std::printf("%-8s %7.2f %5d | %9.2f %8.4f | %5s | %-34s | %4d\n", "linear", c.spec.attenuationDb,
                  c.spec.primeBlocks, f.rejectionDb, f.rippleDb, c.primeOk ? "ok" : "FAIL", latencies.c_str(),
                  c.worst48);
    }
  }
  // Ruling 1's rule: the lowest worst-case 48 kHz input-path latency; ties to linear phase, then
  // to the higher attenuation.
  const Candidate* best = nullptr;
  for (const Candidate& c : candidates) {
    if (!c.floorsMet || !c.primeOk || !c.alignmentOk) continue;
    auto rank = [](const Candidate& x) {
      return std::make_tuple(x.worst48, x.spec.phase == ConverterPhase::Linear ? 0 : 1, -x.spec.attenuationDb,
                             -x.spec.primeBlocks);
    };
    if (best == nullptr || rank(c) < rank(*best)) best = &c;
  }
  REQUIRE(best != nullptr);
  std::printf("Selected: %s phase, %.2f dB, prime %d native blocks (worst 48 kHz input path %d samples)\n",
              best->spec.phase == ConverterPhase::Linear ? "linear" : "minimum", best->spec.attenuationDb,
              best->spec.primeBlocks, best->worst48);
  // Ruling 3's alignment figures of the linear-phase choice and Studio, for comparison.
  for (const ConverterSpec& spec : {best->spec, converterSpec(ConverterQuality::Studio)}) {
    Alignment worst;
    for (double host : kHosts) {
      for (const Native& native : kNatives) {
        if (bypassed(host, native.rate)) continue;
        const Alignment a = alignment(spec, host, native);
        worst.worstOutputError = std::max(worst.worstOutputError, a.worstOutputError);
        worst.worstInputError = std::max(worst.worstInputError, a.worstInputError);
        worst.deepestNotchDb = std::min(worst.deepestNotchDb, a.deepestNotchDb);
      }
    }
    std::printf("  %s %.2f dB: impulse error out %d in %d samples; deepest Dry + wet notch below f_pass %.2f dB\n",
                spec.phase == ConverterPhase::Linear ? "linear" : "minimum", spec.attenuationDb,
                worst.worstOutputError, worst.worstInputError, worst.deepestNotchDb);
  }
  // What binds: the round trip's level at Studio's f_pass, and where each linear candidate itself
  // first falls 0.1 dB below 1 kHz (48 kHz host).
  std::printf("Linear candidates at a 48 kHz host: level at Studio's f_pass (dB) / own -0.1 dB edge (Hz)\n");
  for (double atten : {96.0, 108.0, 120.0, 136.0, 150.0, 180.15}) {
    const ConverterSpec spec{ConverterPhase::Linear, atten, NativeRateWrapper::kTransitionBandPercent, 2};
    std::string row;
    for (const Native& native : kNatives) {
      if (bypassed(48000.0, native.rate)) continue;
      RoundTrip trip(spec, 48000.0, native);
      const double ref = trip.levelDb(1000.0);
      const double edge = passbandEdge(48000.0, native);
      double own = 1100.0;
      while (own < 24000.0 && trip.levelDb(own) >= ref - kRippleFloorDb) own += 100.0;
      char cell[64];
      std::snprintf(cell, sizeof(cell), "  %.0f k: %+.2f / %5.0f", native.rate / 1000.0,
                    trip.levelDb(edge - 100.0) - ref, own);
      row += cell;
    }
    std::printf("  %6.2f dB%s\n", atten, row.c_str());
  }
  // Per rate pair, Live (as built) vs Studio.
  std::printf("Per rate pair (selected | Studio): rejection dB, ripple dB, f_pass, input / output latency\n");
  for (double host : kHosts) {
    for (const Native& native : kNatives) {
      if (bypassed(host, native.rate)) continue;
      const ConverterSpec studio = converterSpec(ConverterQuality::Studio);
      std::printf("  %6.0f <-> %8.2f: %7.2f | %7.2f   %.4f | %.4f   %6.0f Hz   %4d / %4d | %4d / %4d\n", host,
                  native.rate, stopBandRejectionDb(best->spec, host, native), stopBandRejectionDb(studio, host, native),
                  passbandRippleDb(best->spec, host, native), passbandRippleDb(studio, host, native),
                  passbandEdge(host, native), inputPathLatency(best->spec, host, native),
                  outputPathLatency(best->spec, host, native), inputPathLatency(studio, host, native),
                  outputPathLatency(studio, host, native));
    }
  }
}
