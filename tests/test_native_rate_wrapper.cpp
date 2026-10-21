#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <vector>

#include "alloc_counter.h"
#include "engine/native_rate_wrapper.h"
#include "engine/plaits_engine.h"
#include "test_engines.h"
#include "test_helpers.h"

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
  auto wrapped = wrappedPlaits();
  PlaitsEngine direct;
  wrapped->prepare(PlaitsEngine::kNativeRate, 512);
  direct.prepare(PlaitsEngine::kNativeRate, 512);
  REQUIRE(wrapped->latencySamples() == 0);
  const auto p = test::defaultParams(PlaitsEngine::staticInfo());
  wrapped->noteOn(60.0f, 1.0f);
  direct.noteOn(60.0f, 1.0f);
  REQUIRE(render(*wrapped, p, 4800, 256) == render(direct, p, 4800, 256));
}

TEST_CASE("NativeRateWrapper keeps Plaits in tune at common host rates") {
  const auto p = test::defaultParams(PlaitsEngine::staticInfo());
  for (double rate : {44100.0, 48000.0, 88200.0, 96000.0, 192000.0}) {
    auto wrapped = wrappedPlaits();
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
  for (double rate : {44100.0, 96000.0, 192000.0}) {
    NativeRateWrapper wrapped(std::make_unique<test::ImpulseEngine>(), 48000.0, 12);
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
  const auto p = test::defaultParams(PlaitsEngine::staticInfo());
  auto a = wrappedPlaits();
  auto b = wrappedPlaits();
  auto c = wrappedPlaits();
  for (auto* w : {a.get(), b.get(), c.get()}) {
    w->prepare(44100.0, 4096);
    w->noteOn(60.0f, 1.0f);
  }
  const auto ra = render(*a, p, 9000, 4096);
  REQUIRE(ra == render(*b, p, 9000, 1));
  REQUIRE(ra == render(*c, p, 9000, 7));
}

TEST_CASE("NativeRateWrapper process does not allocate") {
  auto wrapped = wrappedPlaits();
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
  for (double rate : {44100.0, 48000.0, 96000.0}) {
    NativeRateWrapper w(std::make_unique<test::ImpulseEngine>(), PlaitsEngine::kNativeRate, 12);
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

int peakIndex(const std::vector<float>& x) {
  return int(std::max_element(x.begin(), x.end(), [](float a, float b) {
               return std::fabs(a) < std::fabs(b);
             }) - x.begin());
}

}  // namespace

TEST_CASE("A host-rate engine passes input straight through") {
  NativeRateWrapper wrapped(std::make_unique<test::EchoEngine>(), 0.0, 16);
  wrapped.prepare(44100.0, 256);
  REQUIRE(wrapped.latencySamples() == 0);
  REQUIRE(wrapped.inputLatencySamples() == 0);
  std::vector<float> input(1000, 0.0f);
  input[100] = 1.0f;
  const auto out = renderWithInput(wrapped, input, 256);
  REQUIRE(out == input);
}

TEST_CASE("Input path latency is reported and constant") {
  for (double rate : {44100.0, 48000.0, 96000.0}) {
    NativeRateWrapper wrapped(std::make_unique<test::EchoEngine>(), 32000.0, 16);
    wrapped.prepare(rate, 256);
    const int latency = wrapped.inputLatencySamples();
    INFO("rate " << rate << " input latency " << latency);
    REQUIRE(latency > 0);
    REQUIRE(double(latency) / rate <= 0.012);

    std::vector<float> input(8000, 0.0f);
    input[3000] = 1.0f;
    const auto big = renderWithInput(wrapped, input, 256);
    const int delay = peakIndex(big) - 3000;
    const int tolerance = int(std::ceil(16.0 * rate / 32000.0)) + 2;
    INFO("delay " << delay);
    REQUIRE(std::abs(delay - latency) <= tolerance);
    REQUIRE(wrapped.inputUnderruns() == 0);

    NativeRateWrapper other(std::make_unique<test::EchoEngine>(), 32000.0, 16);
    other.prepare(rate, 256);
    REQUIRE(renderWithInput(other, input, 1) == big);
  }
}

TEST_CASE("Extra delay adds exactly that many samples") {
  NativeRateWrapper wrapped(std::make_unique<test::ImpulseEngine>(), 48000.0, 12);
  wrapped.prepare(48000.0, 256);
  wrapped.setExtraDelay(100);
  REQUIRE(wrapped.extraDelaySamples() == 100);
  REQUIRE(wrapped.latencySamples() == 100);
  wrapped.noteOn(60.0f, 1.0f);
  const auto out = render(wrapped, ParamValues{}, 1000, 64);
  REQUIRE(peakIndex(out) == 100);
}

TEST_CASE("Engines that don't use input never see it") {
  NativeRateWrapper wrapped(std::make_unique<test::ImpulseEngine>(), 32000.0, 16);
  wrapped.prepare(48000.0, 256);
  REQUIRE(wrapped.inputLatencySamples() == 0);
  std::vector<float> input(4000, 0.5f);
  const auto out = renderWithInput(wrapped, input, 256);
  for (float s : out) REQUIRE(s == 0.0f);
}

TEST_CASE("Input conversion does not allocate") {
  NativeRateWrapper wrapped(std::make_unique<test::EchoEngine>(), 32000.0, 16);
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
  struct Case { double native; double host; int block; };
  for (const Case c : {Case{32000.0, 22050.0, 16}, Case{48000.0, 32000.0, 24},
                       Case{48000.0, 16000.0, 24}}) {
    INFO("native " << c.native << " host " << c.host);
    const int n = int(3.0 * c.host), at = int(c.host);
    std::vector<float> input(static_cast<size_t>(n), 0.0f);
    input[size_t(at)] = 1.0f;
    for (int mixed = 0; mixed < 2; ++mixed) {
      NativeRateWrapper wrapped(std::make_unique<test::EchoEngine>(), c.native, c.block);
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
      INFO("latency " << latency << " delay " << peakIndex(out) - at);
      REQUIRE(std::abs(peakIndex(out) - at - latency) <= tolerance);
    }
  }
}

TEST_CASE("Host-rate engines are prepared for host-sized blocks") {
  auto probe = std::make_unique<test::MaxBlockProbeEngine>();
  test::MaxBlockProbeEngine* raw = probe.get();
  NativeRateWrapper wrapped(std::move(probe), 0.0, 16);
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
  REQUIRE(sideSourceOf(0.0f) == SideSource::Voice);
  REQUIRE(sideSourceOf(0.26f) == SideSource::SourceAux);
  REQUIRE(sideSourceOf(0.5f) == SideSource::Input);
  REQUIRE(sideSourceOf(1.0f) == SideSource::OtherPart);
  REQUIRE(sideSourceOf(std::nanf("")) == SideSource::Voice);
}

TEST_CASE("Side inputs are converted with the input path's latency") {
  for (double rate : {44100.0, 48000.0}) {
    NativeRateWrapper w(std::make_unique<test::SideEchoEngine>(), 96000.0, 60);
    w.prepare(rate, 256);
    Channels in(8000);
    in.c[2][3000] = 1.0f;  // side 1 -> out L
    in.c[3][3000] = 1.0f;  // side 2 -> out R
    const auto out = runAll(w, in, true, 256);
    INFO("rate " << rate << " input latency " << w.inputLatencySamples());
    REQUIRE(peakIndex(out.c[0]) == peakIndex(out.c[1]));
    REQUIRE(std::abs(peakIndex(out.c[0]) - 3000 - w.inputLatencySamples()) <= 4);
    REQUIRE(w.inputUnderruns() == 0);
  }
}

TEST_CASE("Missing side inputs read as silence") {
  NativeRateWrapper w(std::make_unique<test::SideEchoEngine>(), 96000.0, 60);
  w.prepare(48000.0, 256);
  Channels in(4000);
  for (auto& s : in.c[2]) s = 0.5f;
  for (auto& s : in.c[3]) s = 0.5f;
  const auto out = runAll(w, in, false, 256);
  for (float s : out.c[0]) REQUIRE(s == 0.0f);
  for (float s : out.c[1]) REQUIRE(s == 0.0f);
}

TEST_CASE("AUX is converted like the main outputs") {
  for (double rate : {44100.0, 48000.0}) {
    NativeRateWrapper aux(std::make_unique<test::AuxImpulseEngine>(), 47872.34, 12);
    aux.setAuxEnabled(true);
    aux.prepare(rate, 256);
    NativeRateWrapper main(std::make_unique<test::ImpulseEngine>(), 47872.34, 12);
    main.prepare(rate, 256);
    REQUIRE(aux.latencySamples() == main.latencySamples());
    aux.noteOn(60.0f, 1.0f);
    main.noteOn(60.0f, 1.0f);
    Channels silent(2000);
    const auto a = runAll(aux, silent, false, 100);
    const auto m = runAll(main, silent, false, 100);
    REQUIRE(peakIndex(a.c[2]) == peakIndex(m.c[0]));
    for (float s : a.c[0]) REQUIRE(s == 0.0f);
  }
}

// Task 2 carry-forward: AUX in bypass (the engine at the host rate) and through the extra delay.
TEST_CASE("AUX passes through a bypassed wrapper bit for bit") {
  NativeRateWrapper aux(std::make_unique<test::AuxImpulseEngine>(), 48000.0, 12);
  aux.setAuxEnabled(true);
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
  for (double rate : {48000.0, 44100.0}) {  // 48 kHz: bypassed; 44.1 kHz: converted
    NativeRateWrapper delayed(std::make_unique<test::AuxImpulseEngine>(), 48000.0, 12);
    NativeRateWrapper plain(std::make_unique<test::AuxImpulseEngine>(), 48000.0, 12);
    delayed.setAuxEnabled(true);
    plain.setAuxEnabled(true);
    delayed.prepare(rate, 256);
    plain.prepare(rate, 256);
    delayed.setExtraDelay(100);
    REQUIRE(delayed.latencySamples() == plain.latencySamples() + 100);
    delayed.noteOn(60.0f, 1.0f);
    plain.noteOn(60.0f, 1.0f);
    const auto a = runAll(delayed, Channels(2000), false, 37);
    const auto b = runAll(plain, Channels(2000), false, 37);
    INFO("rate " << rate);
    REQUIRE(peakIndex(a.c[2]) == peakIndex(b.c[2]) + 100);
    for (size_t i = 0; i + 100 < a.c[2].size(); ++i) REQUIRE(a.c[2][i + 100] == b.c[2][i]);
    for (float s : a.c[0]) REQUIRE(s == 0.0f);
    if (rate == 48000.0) REQUIRE(peakIndex(a.c[2]) == 100);
  }
}

TEST_CASE("An engine's own latency is added to the wrapper's") {
  NativeRateWrapper bypassed(std::make_unique<test::FixedLatencyEngine>(), 96000.0, 60);
  bypassed.prepare(96000.0, 256);
  REQUIRE(bypassed.inputLatencySamples() == 60);
  REQUIRE(bypassed.latencySamples() == 60);
  Channels in(2000);
  in.c[0][500] = 1.0f;
  REQUIRE(peakIndex(runAll(bypassed, in, false, 37).c[0]) == 560);

  NativeRateWrapper fixed(std::make_unique<test::FixedLatencyEngine>(), 96000.0, 60);
  NativeRateWrapper echo(std::make_unique<test::EchoEngine>(), 96000.0, 60);
  fixed.prepare(48000.0, 256);
  echo.prepare(48000.0, 256);
  REQUIRE(fixed.inputLatencySamples() - echo.inputLatencySamples() == 30);
}

TEST_CASE("setStereo reaches the wrapped engine") {
  auto probe = std::make_unique<test::StereoProbeEngine>();
  test::StereoProbeEngine* raw = probe.get();
  NativeRateWrapper w(std::move(probe), 0.0, 16);
  w.prepare(48000.0, 256);
  w.setStereo(true);
  REQUIRE(raw->stereo());
  w.setStereo(false);
  REQUIRE_FALSE(raw->stereo());
}

TEST_CASE("Four-channel conversion does not allocate") {
  NativeRateWrapper w(std::make_unique<test::SideEchoEngine>(), 96000.0, 60);
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
