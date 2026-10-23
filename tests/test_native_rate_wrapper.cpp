#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <vector>

#include "alloc_counter.h"
#include "engine/engine_registry.h"
#include "engine/native_rate_wrapper.h"
#include "engine/plaits_engine.h"
#include "test_engines.h"
#include "test_helpers.h"
#include "voice/processor_factory.h"

using namespace eg13;

namespace {

std::vector<float> render(VoiceEngine& engine, const ParamValues& p, int n, int chunk) {
  std::vector<float> left(static_cast<size_t>(n)), right(static_cast<size_t>(n));
  for (int done = 0; done < n;) {
    const int count = std::min(chunk, n - done);
    float* out[2] = {left.data() + done, right.data() + done};
    engine.process(nullptr, out, p, count);
    done += count;
  }
  return left;
}

std::unique_ptr<NativeRateWrapper> wrappedPlaits() {
  return std::make_unique<NativeRateWrapper>(std::make_unique<PlaitsEngine>(), PlaitsEngine::kNativeRate, 12);
}

}  // namespace

TEST_CASE("NativeRateWrapper is a bit-exact pass-through at the native rate") {
  const auto q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  auto wrapped = wrappedPlaits();
  PlaitsEngine direct;
  wrapped->setQuality(q);
  wrapped->prepare(PlaitsEngine::kNativeRate, 512);
  direct.prepare(PlaitsEngine::kNativeRate, 512);
  REQUIRE(wrapped->latencySamples() == 0);
  const auto p = test::defaultParams(PlaitsEngine::staticInfo());
  wrapped->noteOn(60.0f, 1.0f);
  direct.noteOn(60.0f, 1.0f);
  REQUIRE(render(*wrapped, p, 4800, 256) == render(direct, p, 4800, 256));
}

TEST_CASE("NativeRateWrapper keeps Plaits in tune at common host rates") {
  const auto q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  const auto p = test::defaultParams(PlaitsEngine::staticInfo());
  for (double rate : {44100.0, 48000.0, 88200.0, 96000.0, 192000.0}) {
    auto wrapped = wrappedPlaits();
    wrapped->setQuality(q);
    wrapped->prepare(rate, 512);
    wrapped->noteOn(69.0f, 1.0f);
    const int n = int(rate * 1.5);
    const auto out = render(*wrapped, p, n, 512);
    const double f = test::estimateFrequency(out, rate, size_t(rate * 0.5), size_t(n));
    INFO("rate " << rate << " measured " << f);
    REQUIRE_THAT(f, Catch::Matchers::WithinRel(440.0, 0.001));
  }
}

TEST_CASE("NativeRateWrapper latency is reported and at most 3 ms") {
  const auto q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  for (double rate : {44100.0, 96000.0, 192000.0}) {
    NativeRateWrapper wrapped(std::make_unique<test::ImpulseEngine>(), 48000.0, 12);
    wrapped.setQuality(q);
    wrapped.prepare(rate, 512);
    const int latency = wrapped.latencySamples();
    INFO("rate " << rate << " latency " << latency);
    REQUIRE(latency > 0);
    REQUIRE(double(latency) / rate <= 0.003);

    const ParamValues p;
    render(wrapped, p, 4000, 512);  // run past the converter's start-up
    wrapped.noteOn(60.0f, 1.0f);
    const auto out = render(wrapped, p, 4000, 512);
    const auto peak = std::max_element(out.begin(), out.end(), [](float a, float b) {
      return std::fabs(a) < std::fabs(b);
    });
    const int delay = int(peak - out.begin());
    const int tolerance = int(std::ceil(12.0 * rate / 48000.0)) + 2;  // one native block
    INFO("delay " << delay);
    REQUIRE(std::abs(delay - latency) <= tolerance);
  }
}

TEST_CASE("NativeRateWrapper output is independent of host block size") {
  const auto q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  const auto p = test::defaultParams(PlaitsEngine::staticInfo());
  auto a = wrappedPlaits();
  auto b = wrappedPlaits();
  auto c = wrappedPlaits();
  for (auto* w : {a.get(), b.get(), c.get()}) {
    w->setQuality(q);
    w->prepare(44100.0, 4096);
    w->noteOn(60.0f, 1.0f);
  }
  const auto ra = render(*a, p, 9000, 4096);
  REQUIRE(ra == render(*b, p, 9000, 1));
  REQUIRE(ra == render(*c, p, 9000, 7));
}

TEST_CASE("NativeRateWrapper process does not allocate") {
  const auto q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  auto wrapped = wrappedPlaits();
  wrapped->setQuality(q);
  wrapped->prepare(44100.0, 512);
  const auto p = test::defaultParams(PlaitsEngine::staticInfo());
  std::vector<float> left(512), right(512);
  float* out[2] = {left.data(), right.data()};
  wrapped->noteOn(60.0f, 1.0f);
  wrapped->process(nullptr, out, p, 512);  // warm up

  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int block = 0; block < 100; ++block) {
      if (block == 50) wrapped->noteOn(67.0f, 1.0f);
      wrapped->process(nullptr, out, p, 512);
    }
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}

TEST_CASE("Onset after reset lands at the reported latency") {
  const auto q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  for (double rate : {44100.0, 48000.0, 96000.0}) {
    NativeRateWrapper w(std::make_unique<test::ImpulseEngine>(), PlaitsEngine::kNativeRate, 12);
    w.setQuality(q);
    w.prepare(rate, 512);
    w.reset();
    w.noteOn(60.0f, 1.0f);
    const auto out = render(w, ParamValues{}, 4000, 512);
    size_t peak = 0;
    for (size_t i = 0; i < out.size(); ++i) {
      if (std::fabs(out[i]) > std::fabs(out[peak])) peak = i;
    }
    const double tolerance = std::ceil(12.0 * rate / PlaitsEngine::kNativeRate) + 2.0;
    INFO("rate " << rate << " peak " << peak << " latency " << w.latencySamples());
    REQUIRE(std::fabs(double(peak) - double(w.latencySamples())) <= tolerance);
  }
}

namespace {

std::vector<float> renderWithInput(VoiceEngine& engine, const std::vector<float>& input, int chunk) {
  const int n = int(input.size());
  std::vector<float> left(static_cast<size_t>(n)), right(static_cast<size_t>(n));
  const ParamValues p;
  for (int done = 0; done < n;) {
    const int count = std::min(chunk, n - done);
    const float* in[2] = {input.data() + done, input.data() + done};
    float* out[2] = {left.data() + done, right.data() + done};
    engine.process(in, out, p, count);
    done += count;
  }
  REQUIRE(right == left);
  return left;
}

}  // namespace

TEST_CASE("A host-rate engine passes input straight through") {
  const auto q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  NativeRateWrapper wrapped(std::make_unique<test::EchoEngine>(), 0.0, 16);
  wrapped.setQuality(q);
  wrapped.prepare(44100.0, 256);
  REQUIRE(wrapped.latencySamples() == 0);
  REQUIRE(wrapped.inputLatencySamples() == 0);
  std::vector<float> input(1000, 0.0f);
  input[100] = 1.0f;
  const auto out = renderWithInput(wrapped, input, 256);
  REQUIRE(out == input);
}

TEST_CASE("Input path latency is reported and constant") {
  const auto q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  for (double rate : {44100.0, 48000.0, 96000.0}) {
    NativeRateWrapper wrapped(std::make_unique<test::EchoEngine>(), 32000.0, 16);
    wrapped.setQuality(q);
    wrapped.prepare(rate, 256);
    const int latency = wrapped.inputLatencySamples();
    INFO("rate " << rate << " input latency " << latency);
    REQUIRE(latency > 0);
    REQUIRE(double(latency) / rate <= 0.012);

    std::vector<float> input(8000, 0.0f);
    input[3000] = 1.0f;
    const auto big = renderWithInput(wrapped, input, 256);
    const int delay = test::peakIndex(big) - 3000;
    const int tolerance = int(std::ceil(16.0 * rate / 32000.0)) + 2;
    INFO("delay " << delay);
    REQUIRE(std::abs(delay - latency) <= tolerance);
    REQUIRE(wrapped.inputUnderruns() == 0);

    NativeRateWrapper other(std::make_unique<test::EchoEngine>(), 32000.0, 16);
    other.setQuality(q);
    other.prepare(rate, 256);
    REQUIRE(renderWithInput(other, input, 1) == big);
  }
}

TEST_CASE("Extra delay adds exactly that many samples") {
  const auto q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  NativeRateWrapper wrapped(std::make_unique<test::ImpulseEngine>(), 48000.0, 12);
  wrapped.setQuality(q);
  wrapped.prepare(48000.0, 256);
  wrapped.setExtraDelay(100);
  REQUIRE(wrapped.extraDelaySamples() == 100);
  REQUIRE(wrapped.latencySamples() == 100);
  wrapped.noteOn(60.0f, 1.0f);
  const auto out = render(wrapped, ParamValues{}, 1000, 64);
  REQUIRE(test::peakIndex(out) == 100);
}

TEST_CASE("Engines that don't use input never see it") {
  const auto q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  NativeRateWrapper wrapped(std::make_unique<test::ImpulseEngine>(), 32000.0, 16);
  wrapped.setQuality(q);
  wrapped.prepare(48000.0, 256);
  REQUIRE(wrapped.inputLatencySamples() == 0);
  std::vector<float> input(4000, 0.5f);
  const auto out = renderWithInput(wrapped, input, 256);
  for (float s : out) REQUIRE(s == 0.0f);
}

TEST_CASE("Input conversion does not allocate") {
  const auto q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  NativeRateWrapper wrapped(std::make_unique<test::EchoEngine>(), 32000.0, 16);
  wrapped.setQuality(q);
  wrapped.prepare(44100.0, 256);
  wrapped.setExtraDelay(37);
  std::vector<float> input(256, 0.25f), l(256), r(256);
  const float* in[2] = {input.data(), input.data()};
  float* out[2] = {l.data(), r.data()};
  const ParamValues p;
  wrapped.process(in, out, p, 256);
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int block = 0; block < 100; ++block) wrapped.process(block % 2 ? in : nullptr, out, p, 256);
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}

TEST_CASE("Input path survives host rates below the native rate") {
  const auto q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  struct Case { double native; double host; int block; };
  for (const Case c : {Case{32000.0, 22050.0, 16}, Case{48000.0, 32000.0, 24},
                       Case{48000.0, 16000.0, 24}}) {
    INFO("native " << c.native << " host " << c.host);
    const int n = int(3.0 * c.host), at = int(c.host);
    std::vector<float> input(static_cast<size_t>(n), 0.0f);
    input[size_t(at)] = 1.0f;
    for (int mixed = 0; mixed < 2; ++mixed) {
      NativeRateWrapper wrapped(std::make_unique<test::EchoEngine>(), c.native, c.block);
      wrapped.setQuality(q);
      wrapped.prepare(c.host, 256);
      const int latency = wrapped.inputLatencySamples();
      std::vector<float> out(static_cast<size_t>(n));
      const ParamValues p;
      const int sizes[3] = {1, 129, 256};
      int k = 0;
      for (int done = 0; done < n; ++k) {
        const int count = std::min(mixed ? sizes[k % 3] : 256, n - done);
        const float* in[2] = {input.data() + done, input.data() + done};
        float* o[2] = {out.data() + done, out.data() + done};
        wrapped.process(in, o, p, count);
        done += count;
      }
      REQUIRE(wrapped.inputOverruns() == 0);
      REQUIRE(wrapped.inputUnderruns() == 0);
      const int tolerance = int(std::ceil(double(c.block) * c.host / c.native)) + 2;
      INFO("latency " << latency << " delay " << test::peakIndex(out) - at);
      REQUIRE(std::abs(test::peakIndex(out) - at - latency) <= tolerance);
    }
  }
}

TEST_CASE("Host-rate engines are prepared for host-sized blocks") {
  const auto q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  auto probe = std::make_unique<test::MaxBlockProbeEngine>();
  test::MaxBlockProbeEngine* raw = probe.get();
  NativeRateWrapper wrapped(std::move(probe), 0.0, 16);
  wrapped.setQuality(q);
  wrapped.prepare(44100.0, 512);
  REQUIRE(raw->preparedMax >= 512);
  std::vector<float> l(512), r(512);
  float* out[2] = {l.data(), r.data()};
  wrapped.process(nullptr, out, ParamValues{}, 512);
  REQUIRE(raw->largest == 512);
  REQUIRE(raw->largest <= raw->preparedMax);
}

namespace {

struct Channels {
  std::vector<float> c[4];
  explicit Channels(size_t n) {
    for (auto& v : c) v.assign(n, 0.0f);
  }
};

// Runs a wrapper over four-channel input (L, R, side 1, side 2; the sides nullable) and
// three-channel output (L, R, AUX).
Channels runAll(NativeRateWrapper& w, const Channels& in, bool sides, int chunk) {
  const size_t n = in.c[0].size();
  Channels out(n);
  const ParamValues p;
  for (size_t done = 0; done < n;) {
    const int count = int(std::min<size_t>(size_t(chunk), n - done));
    const float* i4[4] = {in.c[0].data() + done, in.c[1].data() + done, sides ? in.c[2].data() + done : nullptr,
                          sides ? in.c[3].data() + done : nullptr};
    float* o3[3] = {out.c[0].data() + done, out.c[1].data() + done, out.c[2].data() + done};
    w.process(i4, o3, p, count);
    done += size_t(count);
  }
  return out;
}

}  // namespace

TEST_CASE("sideSourceOf steps in quarters") {
  REQUIRE(test::sideSourceOf(0.0f) == SideSource::Voice);
  REQUIRE(test::sideSourceOf(0.26f) == SideSource::SourceAux);
  REQUIRE(test::sideSourceOf(0.5f) == SideSource::Input);
  REQUIRE(test::sideSourceOf(1.0f) == SideSource::OtherPart);
  REQUIRE(test::sideSourceOf(std::nanf("")) == SideSource::Voice);
}

TEST_CASE("Side inputs are converted with the input path's latency") {
  const auto q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  for (double rate : {44100.0, 48000.0}) {
    NativeRateWrapper w(std::make_unique<test::SideEchoEngine>(), 96000.0, 60);
    w.setQuality(q);
    w.prepare(rate, 256);
    Channels in(8000);
    in.c[2][3000] = 1.0f;  // side 1 -> out L
    in.c[3][3000] = 1.0f;  // side 2 -> out R
    const auto out = runAll(w, in, true, 256);
    INFO("rate " << rate << " input latency " << w.inputLatencySamples());
    REQUIRE(test::peakIndex(out.c[0]) == test::peakIndex(out.c[1]));
    REQUIRE(std::abs(test::peakIndex(out.c[0]) - 3000 - w.inputLatencySamples()) <= 4);
    REQUIRE(w.inputUnderruns() == 0);
  }
}

TEST_CASE("Missing side inputs read as silence") {
  const auto q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  NativeRateWrapper w(std::make_unique<test::SideEchoEngine>(), 96000.0, 60);
  w.setQuality(q);
  w.prepare(48000.0, 256);
  Channels in(4000);
  for (auto& s : in.c[2]) s = 0.5f;
  for (auto& s : in.c[3]) s = 0.5f;
  const auto out = runAll(w, in, false, 256);
  for (float s : out.c[0]) REQUIRE(s == 0.0f);
  for (float s : out.c[1]) REQUIRE(s == 0.0f);
}

TEST_CASE("AUX is converted like the main outputs") {
  const auto q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  for (double rate : {44100.0, 48000.0}) {
    NativeRateWrapper aux(std::make_unique<test::AuxImpulseEngine>(), 47872.34, 12);
    aux.setAuxEnabled(true);
    aux.setQuality(q);
    aux.prepare(rate, 256);
    NativeRateWrapper main(std::make_unique<test::ImpulseEngine>(), 47872.34, 12);
    main.setQuality(q);
    main.prepare(rate, 256);
    REQUIRE(aux.latencySamples() == main.latencySamples());
    aux.noteOn(60.0f, 1.0f);
    main.noteOn(60.0f, 1.0f);
    Channels silent(2000);
    const auto a = runAll(aux, silent, false, 100);
    const auto m = runAll(main, silent, false, 100);
    REQUIRE(test::peakIndex(a.c[2]) == test::peakIndex(m.c[0]));
    for (float s : a.c[0]) REQUIRE(s == 0.0f);
  }
}

// Task 2 carry-forward: AUX in bypass (the engine at the host rate) and through the extra delay.
TEST_CASE("AUX passes through a bypassed wrapper bit for bit") {
  const auto q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  NativeRateWrapper aux(std::make_unique<test::AuxImpulseEngine>(), 48000.0, 12);
  aux.setAuxEnabled(true);
  aux.setQuality(q);
  aux.prepare(48000.0, 256);
  REQUIRE(aux.latencySamples() == 0);
  aux.noteOn(60.0f, 1.0f);
  const auto out = runAll(aux, Channels(500), false, 37);
  REQUIRE(out.c[2][0] == 1.0f);
  for (size_t i = 1; i < out.c[2].size(); ++i) REQUIRE(out.c[2][i] == 0.0f);
  for (float s : out.c[0]) REQUIRE(s == 0.0f);
  for (float s : out.c[1]) REQUIRE(s == 0.0f);
}

TEST_CASE("Extra delay delays AUX with the main outputs, bypassed or converted") {
  const auto q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  for (double rate : {48000.0, 44100.0}) {  // 48 kHz: bypassed; 44.1 kHz: converted
    NativeRateWrapper delayed(std::make_unique<test::AuxImpulseEngine>(), 48000.0, 12);
    NativeRateWrapper plain(std::make_unique<test::AuxImpulseEngine>(), 48000.0, 12);
    delayed.setAuxEnabled(true);
    plain.setAuxEnabled(true);
    delayed.setQuality(q);
    delayed.prepare(rate, 256);
    plain.setQuality(q);
    plain.prepare(rate, 256);
    delayed.setExtraDelay(100);
    REQUIRE(delayed.latencySamples() == plain.latencySamples() + 100);
    delayed.noteOn(60.0f, 1.0f);
    plain.noteOn(60.0f, 1.0f);
    const auto a = runAll(delayed, Channels(2000), false, 37);
    const auto b = runAll(plain, Channels(2000), false, 37);
    INFO("rate " << rate);
    REQUIRE(test::peakIndex(a.c[2]) == test::peakIndex(b.c[2]) + 100);
    for (size_t i = 0; i + 100 < a.c[2].size(); ++i) REQUIRE(a.c[2][i + 100] == b.c[2][i]);
    for (float s : a.c[0]) REQUIRE(s == 0.0f);
    if (rate == 48000.0) REQUIRE(test::peakIndex(a.c[2]) == 100);
  }
}

TEST_CASE("An engine's own latency is added to the wrapper's") {
  const auto q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  NativeRateWrapper bypassed(std::make_unique<test::FixedLatencyEngine>(), 96000.0, 60);
  bypassed.setQuality(q);
  bypassed.prepare(96000.0, 256);
  REQUIRE(bypassed.inputLatencySamples() == 60);
  REQUIRE(bypassed.latencySamples() == 60);
  Channels in(2000);
  in.c[0][500] = 1.0f;
  REQUIRE(test::peakIndex(runAll(bypassed, in, false, 37).c[0]) == 560);

  NativeRateWrapper fixed(std::make_unique<test::FixedLatencyEngine>(), 96000.0, 60);
  NativeRateWrapper echo(std::make_unique<test::EchoEngine>(), 96000.0, 60);
  fixed.setQuality(q);
  fixed.prepare(48000.0, 256);
  echo.setQuality(q);
  echo.prepare(48000.0, 256);
  REQUIRE(fixed.inputLatencySamples() - echo.inputLatencySamples() == 30);
}

TEST_CASE("setStereo reaches the wrapped engine") {
  const auto q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  auto probe = std::make_unique<test::StereoProbeEngine>();
  test::StereoProbeEngine* raw = probe.get();
  NativeRateWrapper w(std::move(probe), 0.0, 16);
  w.setQuality(q);
  w.prepare(48000.0, 256);
  w.setStereo(true);
  REQUIRE(raw->stereo());
  w.setStereo(false);
  REQUIRE_FALSE(raw->stereo());
}

TEST_CASE("Four-channel conversion does not allocate") {
  const auto q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  NativeRateWrapper w(std::make_unique<test::SideEchoEngine>(), 96000.0, 60);
  w.setQuality(q);
  w.prepare(44100.0, 256);
  w.setExtraDelay(11);
  std::vector<float> a(256, 0.25f), l(256), r(256), x(256);
  const float* in[4] = {a.data(), a.data(), a.data(), a.data()};
  float* out[3] = {l.data(), r.data(), x.data()};
  const ParamValues p;
  w.process(in, out, p, 256);
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int block = 0; block < 100; ++block) {
      in[2] = block % 3 ? a.data() : nullptr;
      in[3] = block % 5 ? a.data() : nullptr;
      w.process(in, out, p, 256);
    }
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}

// Ruling F19 (Task 15): channels equal to their reference (R to L, side inputs to silence) are
// not converted. Each case runs one wrapper with skipping and one converting every channel
// always, and requires the same output sample for sample, through every switch.
namespace {

struct SkipRun {
  Channels out;
  int fewestIn = 99;
  int fewestOut = 99;
  int mostIn = 0;
  int mostOut = 0;
  std::vector<int> inAt;  // converted input channels after each chunk
};

// present(start): whether side input k is passed (not null) for the chunk starting at `start`.
template <typename Present>
SkipRun runSkipping(NativeRateWrapper& w, const Channels& in, int chunk, Present present,
                    bool stereoHint = false) {
  const size_t n = in.c[0].size();
  SkipRun run{Channels(n)};
  const ParamValues p;
  for (size_t done = 0; done < n;) {
    const int count = int(std::min<size_t>(size_t(chunk), n - done));
    const float* i4[4] = {in.c[0].data() + done, in.c[1].data() + done,
                          present(0, done) ? in.c[2].data() + done : nullptr,
                          present(1, done) ? in.c[3].data() + done : nullptr};
    float* o3[3] = {run.out.c[0].data() + done, run.out.c[1].data() + done, run.out.c[2].data() + done};
    if (stereoHint) {  // as a ProcessorSlot does: stereo while L != R anywhere in the chunk
      bool stereo = false;
      for (int i = 0; i < count; ++i) stereo = stereo || i4[0][i] != i4[1][i];
      w.setStereo(stereo);
    }
    w.process(i4, o3, p, count);
    run.fewestIn = std::min(run.fewestIn, w.convertedInputChannels());
    run.fewestOut = std::min(run.fewestOut, w.convertedOutputChannels());
    run.mostIn = std::max(run.mostIn, w.convertedInputChannels());
    run.mostOut = std::max(run.mostOut, w.convertedOutputChannels());
    run.inAt.push_back(w.convertedInputChannels());
    done += size_t(count);
  }
  return run;
}

float testSignal(size_t i, double f) {
  return float(0.5 * std::sin(f * double(i)) + 0.2 * std::sin(0.37 * double(i)));
}

double largestDifference(const Channels& a, const Channels& b, int channels) {
  double worst = 0.0;
  for (int ch = 0; ch < channels; ++ch) {
    for (size_t i = 0; i < a.c[ch].size(); ++i) {
      worst = std::max(worst, double(std::fabs(a.c[ch][i] - b.c[ch][i])));
    }
  }
  return worst;
}

}  // namespace

TEST_CASE("Side inputs that drop out and come back match always-on conversion") {
  const auto q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  for (double rate : {44100.0, 48000.0, 88200.0}) {
    for (int chunk : {16, 37}) {
      NativeRateWrapper skipping(std::make_unique<test::SideEchoEngine>(), 96000.0, 60);
      NativeRateWrapper always(std::make_unique<test::SideEchoEngine>(), 96000.0, 60);
      always.setSkipUnused(false);
      skipping.setQuality(q);
      skipping.prepare(rate, 256);
      always.setQuality(q);
      always.prepare(rate, 256);
      Channels in(30000);
      for (size_t i = 0; i < 30000; ++i) {
        in.c[0][i] = testSignal(i, 0.01);
        in.c[1][i] = testSignal(i, 0.011);  // stereo throughout: only the side inputs skip
        // Side 1: present but silent over [15000, 21000).
        in.c[2][i] = i >= 15000 && i < 21000 ? 0.0f : testSignal(i, 0.02);
        in.c[3][i] = testSignal(i, 0.03);
      }
      // Side 1 absent over [3000, 9000); side 2 absent over [6000, 24000), back for the rest.
      auto present = [](int k, size_t at) {
        return k == 0 ? !(at >= 3000 && at < 9000) : !(at >= 6000 && at < 24000);
      };
      const SkipRun a = runSkipping(skipping, in, chunk, present);
      const SkipRun b = runSkipping(always, in, chunk, present);
      INFO("rate " << rate << " chunk " << chunk);
      REQUIRE(b.fewestIn == 4);
      REQUIRE(a.fewestIn == 2);  // both side inputs skipped at once (over [6000, 9000))
      REQUIRE(a.mostIn == 4);    // and both converted again
      REQUIRE(largestDifference(a.out, b.out, 2) <= 1e-6);
      REQUIRE(test::rms(a.out.c[0], 25000, 30000) > 0.1);  // the side signal came back
    }
  }
}

TEST_CASE("Mono, then stereo, then mono again matches always-on conversion") {
  const auto q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  for (double rate : {44100.0, 48000.0}) {
    for (int chunk : {16, 37}) {
      NativeRateWrapper skipping(std::make_unique<test::StereoProbeEngine>(), 96000.0, 60);
      NativeRateWrapper always(std::make_unique<test::StereoProbeEngine>(), 96000.0, 60);
      always.setSkipUnused(false);
      skipping.setQuality(q);
      skipping.prepare(rate, 256);
      always.setQuality(q);
      always.prepare(rate, 256);
      Channels in(30000);
      for (size_t i = 0; i < 30000; ++i) {
        in.c[0][i] = testSignal(i, 0.01);
        in.c[1][i] = i >= 10000 && i < 18000 ? testSignal(i, 0.013) : in.c[0][i];
      }
      auto none = [](int, size_t) { return false; };
      const SkipRun a = runSkipping(skipping, in, chunk, none);
      const SkipRun b = runSkipping(always, in, chunk, none);
      INFO("rate " << rate << " chunk " << chunk);
      REQUIRE(b.fewestIn == 2);
      REQUIRE(b.fewestOut == 2);
      REQUIRE(a.fewestIn == 1);   // R input skipped while mono
      REQUIRE(a.fewestOut == 1);  // R output skipped while mono
      REQUIRE(a.mostIn == 2);
      REQUIRE(a.mostOut == 2);
      REQUIRE(largestDifference(a.out, b.out, 2) <= 1e-6);
    }
  }
}

TEST_CASE("Ring Mod / Vocoder going mono, stereo and mono matches always-on conversion") {
  const auto q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  for (double rate : {44100.0, 48000.0}) {
    SourceSpec s1 = makeShaper(int(ShaperEngine::RingMod));
    SourceSpec s2 = makeShaper(int(ShaperEngine::RingMod));
    NativeRateWrapper skipping(std::move(s1.engine), s1.nativeRate, s1.nativeBlock);
    NativeRateWrapper always(std::move(s2.engine), s2.nativeRate, s2.nativeBlock);
    always.setSkipUnused(false);
    skipping.setQuality(q);
    skipping.prepare(rate, 16);
    always.setQuality(q);
    always.prepare(rate, 16);
    skipping.noteOn(45.0f, 1.0f);
    always.noteOn(45.0f, 1.0f);
    Channels in(rate == 48000.0 ? 48000 : 44100);
    for (size_t i = 0; i < in.c[0].size(); ++i) {
      in.c[0][i] = testSignal(i, 0.01);
      in.c[1][i] = i >= 12000 && i < 24000 ? testSignal(i, 0.013) : in.c[0][i];
    }
    // The Warps defaults: Internal Osc carrier, the voice as modulator: no side input used.
    auto none = [](int, size_t) { return false; };
    const SkipRun a = runSkipping(skipping, in, 16, none, true);
    const SkipRun b = runSkipping(always, in, 16, none, true);
    INFO("rate " << rate);
    REQUIRE(a.fewestIn == 1);
    REQUIRE(a.fewestOut == 1);
    REQUIRE(a.inAt[20000 / 16] == 2);  // stereo: input R converted again (the sides stay skipped)
    REQUIRE(a.mostOut == 2);
    REQUIRE(test::rms(a.out.c[1], 14000, 22000) > 0.01);
    REQUIRE(largestDifference(a.out, b.out, 2) <= 1e-6);
  }
}

TEST_CASE("Skipping and resuming conversions allocates nothing") {
  const auto q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  NativeRateWrapper w(std::make_unique<test::SideEchoEngine>(), 96000.0, 60);
  w.setQuality(q);
  w.prepare(44100.0, 64);
  Channels in(20000);
  for (size_t i = 0; i < 20000; ++i) {
    in.c[0][i] = testSignal(i, 0.01);
    in.c[1][i] = i >= 8000 && i < 12000 ? 0.0f : in.c[0][i];  // stereo, then mono again
    in.c[2][i] = in.c[3][i] = testSignal(i, 0.02);
  }
  Channels out(20000);
  const ParamValues p;
  int fewest = 99, most = 0;
  {
    test::ScopedAllocationCounter counter;
    for (size_t done = 0; done < 20000; done += 64) {
      const bool sides = (done / 5000) % 2 == 1;  // absent, present, absent, present
      const float* i4[4] = {in.c[0].data() + done, in.c[1].data() + done, sides ? in.c[2].data() + done : nullptr,
                            sides ? in.c[3].data() + done : nullptr};
      float* o3[3] = {out.c[0].data() + done, out.c[1].data() + done, out.c[2].data() + done};
      w.process(i4, o3, p, int(std::min<size_t>(64, 20000 - done)));
      fewest = std::min(fewest, w.convertedInputChannels());
      most = std::max(most, w.convertedInputChannels());
    }
    const size_t allocations = counter.count();
    REQUIRE(allocations == 0);
  }
  REQUIRE(fewest == 1);
  REQUIRE(most == 4);
}

// Fix round 1: every native rate in use (Modal Synth 32 kHz in 16-sample blocks, Resonator
// 48 kHz in 24, Digital Oscillator 96 kHz in 24, Ring Mod / Vocoder 96 kHz in 60) against every
// common host rate, including those where r8brain adds 2:1 stages (176.4, 192 kHz). R goes
// stereo and back three times and the side inputs drop out and come back, at onsets that land
// on odd and even multiples of the converter period.
TEST_CASE("Skipping matches always-on conversion at every native and host rate") {
  const auto q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  struct Native {
    double rate;
    int block;
  };
  const Native natives[] = {{32000.0, 16}, {48000.0, 24}, {96000.0, 24}, {96000.0, 60}};
  for (const Native& native : natives) {
    for (double host : {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0}) {
      if (host == native.rate) continue;  // bypassed
      for (int chunk : {16, 37}) {
        const size_t n = size_t(host * 0.6);
        Channels in(n);
        for (size_t i = 0; i < n; ++i) {
          in.c[0][i] = testSignal(i, 0.01);
          const bool stereo = (i >= n / 6 && i < n / 6 + 3001) || (i >= n / 2 + 17 && i < n / 2 + 2600) ||
                              (i >= 4 * n / 5 + 331 && i < 4 * n / 5 + 1900);
          in.c[1][i] = stereo ? testSignal(i, 0.013) : in.c[0][i];
          in.c[2][i] = testSignal(i, 0.02);
          in.c[3][i] = i >= n / 3 && i < n / 3 + 4000 ? 0.0f : testSignal(i, 0.03);
        }
        auto present = [n](int k, size_t at) {
          return k == 0 ? !((at >= n / 8 && at < n / 4 + 101) || (at >= 2 * n / 3 + 13 && at < 3 * n / 4))
                        : !(at >= n / 2 && at < 3 * n / 5 + 7);
        };
        for (int engine = 0; engine < 2; ++engine) {
          auto make = [engine, &native]() -> std::unique_ptr<NativeRateWrapper> {
            std::unique_ptr<VoiceEngine> inner;
            if (engine == 0) {
              inner = std::make_unique<test::StereoProbeEngine>();
            } else {
              inner = std::make_unique<test::SideEchoEngine>();
            }
            return std::make_unique<NativeRateWrapper>(std::move(inner), native.rate, native.block);
          };
          auto skipping = make();
          auto always = make();
          always->setSkipUnused(false);
          skipping->setQuality(q);
          skipping->prepare(host, 64);
          always->setQuality(q);
          always->prepare(host, 64);
          const SkipRun a = runSkipping(*skipping, in, chunk, present);
          const SkipRun b = runSkipping(*always, in, chunk, present);
          INFO("native " << native.rate << "/" << native.block << " host " << host << " chunk " << chunk
                         << (engine == 0 ? " stereo probe" : " side echo") << " input skip "
                         << skipping->inputSkipEnabled() << " output skip " << skipping->outputSkipEnabled());
          REQUIRE(skipping->skipFallbacks() == 0);
          REQUIRE(largestDifference(a.out, b.out, 2) <= 1e-6);
          const int inChannels = engine == 0 ? 2 : 4;
          if (skipping->inputSkipEnabled()) {
            REQUIRE(a.fewestIn < inChannels);
            REQUIRE(a.mostIn == inChannels);
          }
          if (skipping->outputSkipEnabled() && engine == 0) {
            REQUIRE(a.fewestOut == 1);
            REQUIRE(a.mostOut == 2);
          }
        }
      }
    }
  }
}

TEST_CASE("A resumed converter out of step falls back to converting always") {
  const auto q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  // 176.4 kHz to 32 kHz: r8brain adds a 2:1 stage, so the true period is 882. Forcing the ratio's
  // own period, 441, puts a resumption at an odd multiple one sample out of step.
  NativeRateWrapper w(std::make_unique<test::StereoProbeEngine>(), 32000.0, 16);
  w.setQuality(q);
  w.prepare(176400.0, 64);
  REQUIRE(w.inputSkipEnabled());
  w.forceSkipPeriodsForTest(441, 0);
  const size_t n = 120000;
  Channels in(n);
  for (size_t i = 0; i < n; ++i) {
    in.c[0][i] = testSignal(i, 0.01);
    // Stereo bursts at many onsets, so some resume at an odd multiple of 441.
    const size_t phase = i % 10000;
    in.c[1][i] = phase >= 6000 + 441 * ((i / 10000) % 3) && phase < 8000 ? testSignal(i, 0.013) : in.c[0][i];
  }
  auto none = [](int, size_t) { return false; };
  const SkipRun a = runSkipping(w, in, 16, none);
  REQUIRE(w.skipFallbacks() == 1);
  REQUIRE_FALSE(w.inputSkipEnabled());
  REQUIRE(w.convertedInputChannels() == 2);
  for (int ch = 0; ch < 2; ++ch) {
    for (float x : a.out.c[ch]) REQUIRE(std::fabs(x) <= 1.0f);
  }
}

TEST_CASE("Review case: the Modal Synth's input at 176.4 kHz resumes in step") {
  const auto q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  // Fix round 1, Critical 1: stereo onsets at 20480 and 23111 resumed one sample out of step.
  for (int maxBlock : {64, 256, 512}) {
    for (size_t onset : {size_t(20000), size_t(20480), size_t(23111)}) {
      NativeRateWrapper skipping(std::make_unique<test::StereoProbeEngine>(), 32000.0, 16);
      NativeRateWrapper always(std::make_unique<test::StereoProbeEngine>(), 32000.0, 16);
      always.setSkipUnused(false);
      skipping.setQuality(q);
      skipping.prepare(176400.0, maxBlock);
      always.setQuality(q);
      always.prepare(176400.0, maxBlock);
      Channels in(60000);
      for (size_t i = 0; i < 60000; ++i) {
        in.c[0][i] = testSignal(i, 0.01);
        in.c[1][i] = i >= onset ? testSignal(i, 0.013) : in.c[0][i];
      }
      auto none = [](int, size_t) { return false; };
      const SkipRun a = runSkipping(skipping, in, 64, none);
      const SkipRun b = runSkipping(always, in, 64, none);
      INFO("maxBlock " << maxBlock << " onset " << onset);
      REQUIRE(skipping.inputSkipEnabled());
      REQUIRE(a.fewestIn == 1);
      REQUIRE(skipping.skipFallbacks() == 0);
      REQUIRE(largestDifference(a.out, b.out, 2) <= 1e-6);
    }
  }
}

// After a fallback the wrapper restarts as reset() would (a one-off gap), so the output is back
// in time with an always-converting wrapper: the input-to-output delay stays the reported one.
TEST_CASE("After a fallback the output is aligned again") {
  const auto q = GENERATE(ConverterQuality::Live, ConverterQuality::Studio);
  struct Case {
    int64_t inputPeriod;
    int64_t outputPeriod;
  };
  // 176.4 kHz <-> 32 kHz, true periods 882 (in) and 80 (out); 441 and 40 are one stage short.
  for (const Case c : {Case{441, 0}, Case{0, 40}}) {
    NativeRateWrapper w(std::make_unique<test::StereoProbeEngine>(), 32000.0, 16);
    NativeRateWrapper always(std::make_unique<test::StereoProbeEngine>(), 32000.0, 16);
    always.setSkipUnused(false);
    w.setQuality(q);
    w.prepare(176400.0, 64);
    always.setQuality(q);
    always.prepare(176400.0, 64);
    w.forceSkipPeriodsForTest(c.inputPeriod, c.outputPeriod);
    const size_t n = 120000;
    Channels in(n);
    for (size_t i = 0; i < n; ++i) {
      in.c[0][i] = float(0.5 * std::sin(0.01 * double(i)) + 0.2 * std::sin(0.023 * double(i)));
      const size_t phase = i % 10000;
      const bool burst = i < 80000 && phase >= 6000 + 441 * ((i / 10000) % 3) + 7 * (i / 10000) && phase < 8000;
      in.c[1][i] = burst ? float(0.4 * std::sin(0.017 * double(i))) : in.c[0][i];
    }
    auto none = [](int, size_t) { return false; };
    const SkipRun a = runSkipping(w, in, 16, none);
    const SkipRun b = runSkipping(always, in, 16, none);
    INFO("forced periods " << c.inputPeriod << " / " << c.outputPeriod);
    REQUIRE(w.skipFallbacks() == 1);
    double worst = 0.0;
    for (int ch = 0; ch < 2; ++ch) {
      for (size_t i = 100000; i < n; ++i) worst = std::max(worst, double(std::fabs(a.out.c[ch][i] - b.out.c[ch][i])));
    }
    INFO("largest difference after the fallback " << worst);
    REQUIRE(worst <= 1e-3);
  }
}
