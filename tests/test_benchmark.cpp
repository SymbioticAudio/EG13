#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

#include "engine/engine_registry.h"
#include "engine/warps_engine.h"
#include "params/param_ids.h"
#include "synth/param_table.h"
#include "synth/synth.h"

using namespace eg13;

TEST_CASE("CPU: 16 voices per engine stay within budget", "[.benchmark]") {
  constexpr double kRate = 48000.0;
  constexpr int kBlock = 128;
  constexpr int kSeconds = 5;
  double worst = 0.0;
  for (int e = 0; e < kNumSourceEngines; ++e) {
    Synth synth(PluginKind::Instrument);
    synth.setParam(paramSpec(masterParamIndex(MasterParam::Mode)).id, double(PartMode::Layer));
    for (int part = 0; part < kNumParts; ++part) {
      synth.setParam(paramSpec(sourceEngineParamIndex(part)).id, e);
    }
    synth.prepare(kRate, kBlock);
    synth.setActive(true);
    std::vector<float> in(kBlock, 0.1f), l(kBlock), r(kBlock);
    const float* input[2] = {in.data(), in.data()};
    float* out[2] = {l.data(), r.data()};
    synth.process(input, out, nullptr, kBlock);  // warm up
    for (int key = 48; key < 56; ++key) synth.noteOn(0, key, 0.8f);
    const int blocks = int(kRate) * kSeconds / kBlock;
    const auto start = std::chrono::steady_clock::now();
    for (int b = 0; b < blocks; ++b) {
      if (b % 375 == 0) {  // re-strike every second for the percussive engines
        for (int key = 48; key < 56; ++key) synth.noteOn(0, key, 0.8f);
      }
      synth.process(input, out, nullptr, kBlock);
    }
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const double load = 100.0 * seconds / double(kSeconds);
    std::printf("%-20s %6.2f%% of one core (16 voices, 48 kHz, 128-sample blocks)\n",
                sourceEngineInfo(e).displayName, load);
    worst = load > worst ? load : worst;
    synth.setActive(false);
  }
  REQUIRE(worst < 50.0);
}

namespace {

struct ShaperSetup {
  const char* name;
  int engine;
  float algorithm;  // Ring Mod P1
  int carrier;      // Ring Mod P5 option
  int modulator;    // Ring Mod P6 option
  float mode;       // Ring Mod P9
};

const ShaperSetup kShapers[] = {
    {"Off", 0, 0.0f, 0, 0, 0.0f},
    {"Ring Mod (int osc)", int(ShaperEngine::RingMod), 0.25f, 0, 0, 0.25f},
    {"Ring Mod (voice carrier)", int(ShaperEngine::RingMod), 0.25f, 1, 0, 0.25f},
    {"Vocoder", int(ShaperEngine::RingMod), 1.0f, 0, 0, 0.25f},
    {"Freq shifter", int(ShaperEngine::RingMod), 0.9f, 0, 0, 0.75f},
    {"EQ", int(ShaperEngine::Eq), 0.0f, 0, 0, 0.0f},
};

// Percent of one core for 2 x 8 voices of one Source -> Filter -> Shaper chain at 48 kHz,
// 128-sample blocks. stereo = false keeps the Source mono (identical input channels, Resonator
// Spread 0); stereo = true feeds different channels and keeps the engines' defaults.
double chainLoad(int source, int filter, const ShaperSetup& shaper, int seconds, bool stereo) {
  constexpr double kRate = 48000.0;
  constexpr int kBlock = 128;
  Synth synth(PluginKind::Instrument);
  synth.setParam(paramSpec(masterParamIndex(MasterParam::Mode)).id, double(PartMode::Layer));
  for (int part = 0; part < kNumParts; ++part) {
    synth.setParam(paramSpec(sourceEngineParamIndex(part)).id, source);
    if (!stereo && source == int(SourceEngine::Resonator)) {
      synth.setParam(paramSpec(sourceParamIndex(part, 7)).id, 0.0);  // Spread 0: mono fold
    }
    synth.setParam(paramSpec(filterEngineParamIndex(part)).id, filter);
    synth.setParam(paramSpec(filterParamIndex(part, 0)).id, 0.6);
    synth.setParam(paramSpec(shaperEngineParamIndex(part)).id, shaper.engine);
    if (shaper.engine == int(ShaperEngine::RingMod)) {
      synth.setParam(paramSpec(shaperParamIndex(part, 0)).id, shaper.algorithm);
      synth.setParam(paramSpec(shaperParamIndex(part, 4)).id, WarpsEngine::optionValue(shaper.carrier));
      synth.setParam(paramSpec(shaperParamIndex(part, 5)).id, WarpsEngine::optionValue(shaper.modulator));
      synth.setParam(paramSpec(shaperParamIndex(part, 8)).id, shaper.mode);
    }
  }
  synth.prepare(kRate, kBlock);
  synth.setActive(true);
  std::vector<float> inL(kBlock), inR(kBlock), l(kBlock), r(kBlock);
  for (int i = 0; i < kBlock; ++i) {
    inL[size_t(i)] = 0.1f * float(std::sin(0.05 * i));
    inR[size_t(i)] = stereo ? 0.1f * float(std::cos(0.07 * i)) : inL[size_t(i)];
  }
  const float* input[2] = {inL.data(), inR.data()};
  float* out[2] = {l.data(), r.data()};
  synth.process(input, out, nullptr, kBlock);
  for (int key = 48; key < 56; ++key) synth.noteOn(0, key, 0.8f);
  const int blocks = int(kRate) * seconds / kBlock;
  const auto start = std::chrono::steady_clock::now();
  for (int b = 0; b < blocks; ++b) {
    if (b % 375 == 0) {
      for (int key = 48; key < 56; ++key) synth.noteOn(0, key, 0.8f);
    }
    synth.process(input, out, nullptr, kBlock);
  }
  const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  synth.setActive(false);
  return 100.0 * elapsed / double(seconds);
}

struct Chain {
  int source = 0, filter = 0, shaper = 0;
  double load = 0.0;
};

}  // namespace

TEST_CASE("CPU: Source x Filter x Shaper chains, 2 x 8 voices, mono sources within budget", "[.benchmark]") {
  Chain worst[2];  // [0] mono sources, [1] stereo
  for (int stereo = 0; stereo < 2; ++stereo) {
    for (int s = 0; s < kNumSourceEngines; ++s) {
      if (!stereo && s == int(SourceEngine::Modal)) continue;  // always stereo
      for (int f = 0; f < kNumFilterEngines; ++f) {
        for (int k = 0; k < int(sizeof(kShapers) / sizeof(kShapers[0])); ++k) {
          const double load = chainLoad(s, f, kShapers[k], 1, stereo != 0);
          std::printf("%-7s %-20s %-22s %-26s %6.2f%%\n", stereo ? "stereo" : "mono", sourceEngineInfo(s).displayName,
                      filterEngineInfo(f).displayName, kShapers[k].name, load);
          if (load > worst[stereo].load) worst[stereo] = {s, f, k, load};
        }
      }
    }
  }
  const char* const kinds[2] = {"mono-source", "stereo"};
  double confirmed[2] = {};
  for (int stereo = 0; stereo < 2; ++stereo) {
    const Chain& c = worst[stereo];
    confirmed[stereo] = chainLoad(c.source, c.filter, kShapers[c.shaper], 5, stereo != 0);
    std::printf("Heaviest %s chain: %s -> %s -> %s: %.2f%% of one core over 5 s%s\n", kinds[stereo],
                sourceEngineInfo(c.source).displayName, filterEngineInfo(c.filter).displayName,
                kShapers[c.shaper].name, confirmed[stereo],
                stereo ? " (for information; not held to the budget)" : " (budget 50%)");
  }
  REQUIRE(confirmed[0] < 50.0);
}
