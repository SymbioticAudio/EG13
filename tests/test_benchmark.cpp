#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>
#include <vector>

#include "engine/engine_registry.h"
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
