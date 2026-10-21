#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

#include "Ripples/ripples.hpp"
#include "Shelves/shelves.hpp"
#include "alloc_counter.h"
#include "sst/filters++.h"
#include "test_helpers.h"
#include "warps/dsp/modulator.h"

namespace sfpp = sst::filtersplusplus;

TEST_CASE("The Rack stand-in's RC filter is Rack's bilinear one-pole") {
  rack::dsp::TRCFilter<float> f;
  f.setCutoffFreq(0.01f);
  // Rack include/dsp/filter.hpp:30-49, written out.
  constexpr float kPi = 3.14159265358979f;  // Rack uses M_PI
  const float c = 2.f / (2.f * kPi * 0.01f);
  float x0 = 0.f, y0 = 0.f;
  for (int i = 0; i < 100; ++i) {
    const float x = i == 0 ? 1.f : 0.25f;
    f.process(x);
    const float y = (x + x0 - y0 * (1 - c)) / (1 + c);
    x0 = x;
    y0 = y;
    REQUIRE(f.lowpass() == y);
    REQUIRE(f.highpass() == x - y);
  }
}

TEST_CASE("The Rack stand-in's float_4 behaves per lane") {
  rack::simd::float_4 a(1.f, -2.f, 3.f, -4.f);
  const rack::simd::float_4 b = a * 2.f + 1.f;
  REQUIRE(b[0] == 3.f);
  REQUIRE(b[1] == -3.f);
  REQUIRE(b[3] == -7.f);
  const rack::simd::float_4 c = rack::simd::clamp(a, -1.f, 2.f);  // Rack simd/functions.hpp:251-253
  REQUIRE(c[0] == 1.f);
  REQUIRE(c[1] == -1.f);
  REQUIRE(c[2] == 2.f);
  const rack::simd::float_4 p = rack::simd::pow(2.f, rack::simd::float_4(0.f, 1.f, 2.f, -1.f));
  REQUIRE(std::fabs(p[2] - 4.f) < 1e-5f);
  REQUIRE(std::fabs(p[3] - 0.5f) < 1e-6f);
  a[2] = 9.f;
  REQUIRE(a[2] == 9.f);
  REQUIRE(rack::math::clamp(5.f, 0.f, 1.f) == 1.f);
  REQUIRE(rack::math::rescale(0.5f, 0.f, 1.f, 10.f, 20.f) == 15.f);
}

TEST_CASE("The Rack stand-in's random numbers are in [0, 1) and repeat per seed") {
  rack::random::seed(7);
  std::vector<float> first;
  for (int i = 0; i < 1000; ++i) {
    const float u = rack::random::uniform();
    REQUIRE(u >= 0.f);
    REQUIRE(u < 1.f);
    first.push_back(u);
  }
  rack::random::seed(7);
  for (int i = 0; i < 1000; ++i) REQUIRE(rack::random::uniform() == first[size_t(i)]);
}

TEST_CASE("The VCV Ripples and Shelves models run unmodified") {
  ripples::RipplesEngine ripples;
  ripples.setSampleRate(48000.f);
  ripples::RipplesEngine::Frame frame = {};
  frame.freq_knob = 0.5f;
  frame.res_knob = 0.3f;
  std::vector<float> out(48000);
  for (size_t i = 0; i < out.size(); ++i) {
    frame.input = (i % 100) < 50 ? 5.f : -5.f;
    ripples.process(frame);
    out[i] = frame.lp4;
    REQUIRE(std::isfinite(frame.lp4));
  }
  REQUIRE(eg13::test::rms(out, 4800, 48000) > 0.5);

  shelves::ShelvesEngine eq;
  eq.setSampleRate(48000.f);
  shelves::ShelvesEngine::Frame f = {};
  f.hs_freq_knob = f.p1_freq_knob = f.p2_freq_knob = f.ls_freq_knob = 0.5f;
  f.p1_q_knob = f.p2_q_knob = 0.5f;  // gains 0: flat
  std::vector<float> in(48000), flat(48000);
  for (size_t i = 0; i < in.size(); ++i) {
    in[i] = 5.f * float(std::sin(6.283185307179586 * 1000.0 * double(i) / 48000.0));
    f.main_in = in[i];
    eq.process(f);
    flat[i] = f.main_out;
  }
  const double gainDb = 20.0 * std::log10(eg13::test::rms(flat, 4800, 48000) / eg13::test::rms(in, 4800, 48000));
  INFO("flat EQ gain " << gainDb << " dB");
  REQUIRE(std::fabs(gainDb) < 0.5);
}

TEST_CASE("sst-filters offers 205 filter types and every one prepares") {
  static float lines[4][8192];
  int count = 0;
  for (auto model : sfpp::Filter::availableModels()) {
    for (const auto& config : sfpp::Filter::availableModelConfigurations(model, true)) {
      sfpp::Filter filter;
      filter.setSampleRateAndBlockSize(48000.0, 16);
      for (int lane = 0; lane < 4; ++lane) filter.provideDelayLine(lane, lines[lane]);
      filter.setFilterModel(model);
      filter.setModelConfiguration(config);
      INFO(sfpp::details::FilterPayload::displayName(model, config));
      REQUIRE(filter.prepareInstance());
      ++count;
    }
  }
  REQUIRE(count == 205);
}

TEST_CASE("Switching sst-filters types does not allocate once its tables exist") {
  static float lines[4][8192];
  std::vector<std::pair<sfpp::FilterModel, sfpp::ModelConfig>> all;
  for (auto model : sfpp::Filter::availableModels()) {
    for (const auto& config : sfpp::Filter::availableModelConfigurations(model, true)) all.push_back({model, config});
  }
  sfpp::Filter filter;
  filter.setSampleRateAndBlockSize(48000.0, 16);
  for (int lane = 0; lane < 4; ++lane) filter.provideDelayLine(lane, lines[lane]);
  size_t allocations = 0;
  {
    eg13::test::ScopedAllocationCounter counter;
    for (const auto& [model, config] : all) {
      filter.setFilterModel(model);
      filter.setModelConfiguration(config);
      (void)filter.prepareInstance();
      filter.setStereo();
      filter.makeCoefficients(0, 0.f, 0.5f, 0.5f);
      filter.copyCoefficientsFromVoiceToVoice(0, 1);
      filter.prepareBlock();
      float l = 0.f, r = 0.f;
      for (int i = 0; i < 16; ++i) filter.processStereoSample(0.1f, 0.1f, l, r);
      filter.concludeBlock();
    }
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}

TEST_CASE("Warps' modulator runs from the unmodified upstream sources") {
  auto modulator = std::make_unique<warps::Modulator>();
  std::memset(static_cast<void*>(modulator.get()), 0, sizeof(warps::Modulator));  // Warps.cpp:60
  modulator->Init(96000.0f);
  warps::Parameters* p = modulator->mutable_parameters();
  p->channel_drive[0] = 0.8f;
  p->channel_drive[1] = 0.8f;
  p->modulation_algorithm = 3.0f / 8.0f;  // digital ring modulation
  p->modulation_parameter = 0.5f;
  warps::ShortFrame in[60], out[60];
  std::vector<double> l;
  for (int block = 0; block < 400; ++block) {
    for (int i = 0; i < 60; ++i) {
      const double t = double(block * 60 + i) / 96000.0;
      in[i].l = short(10000.0 * std::sin(6.283185307179586 * 440.0 * t));
      in[i].r = short(10000.0 * std::sin(6.283185307179586 * 110.0 * t));
    }
    modulator->Process(in, out, 60);
    for (const auto& frame : out) l.push_back(double(frame.l));
  }
  // Goertzel magnitude over the second half (after the parameter smoothing settles).
  auto magnitude = [&](double hz) {
    const double c = 2.0 * std::cos(6.283185307179586 * hz / 96000.0);
    double s1 = 0.0, s2 = 0.0;
    for (size_t i = l.size() / 2; i < l.size(); ++i) {
      const double s0 = l[i] + c * s1 - s2;
      s2 = s1;
      s1 = s0;
    }
    return std::sqrt(std::max(0.0, s1 * s1 + s2 * s2 - c * s1 * s2));
  };
  // Ring modulation of 440 Hz by 110 Hz: the sum and difference (550, 330 Hz) dominate, and the
  // carrier itself is suppressed (measured: each about 30 to 40 times the 440 Hz component).
  REQUIRE(magnitude(330.0) > 10.0 * magnitude(440.0));
  REQUIRE(magnitude(550.0) > 10.0 * magnitude(440.0));
}
