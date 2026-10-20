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
