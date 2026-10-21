#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "engine/bypass_engine.h"
#include "engine/engine_registry.h"
#include "synth/param_table.h"
#include "voice/processor_factory.h"
#include "voice/source_factory.h"

using namespace eg13;

TEST_CASE("Engine registry ids are permanent") {
  REQUIRE(int(SourceEngine::Macro) == 0);
  REQUIRE(int(SourceEngine::Digital) == 1);
  REQUIRE(int(SourceEngine::Modal) == 2);
  REQUIRE(int(SourceEngine::Resonator) == 3);
  REQUIRE(int(SourceEngine::AudioInput) == 4);
  REQUIRE(kNumSourceEngines == 5);
  REQUIRE(kSourceEngineSelectorMax == 31);
}

TEST_CASE("Selector values map to engines, unknown values fall back to Macro") {
  REQUIRE(sourceEngineFromSelector(3.0) == 3);
  REQUIRE(sourceEngineFromSelector(2.6) == 3);
  REQUIRE(sourceEngineFromSelector(17.0) == 0);
  REQUIRE(sourceEngineFromSelector(-1.0) == 0);
  REQUIRE(sourceEngineFromSelector(std::numeric_limits<double>::quiet_NaN()) == 0);
  REQUIRE(sourceEngineFromSelector(std::numeric_limits<double>::infinity()) == 0);
  REQUIRE(sourceEngineFromSelector(-std::numeric_limits<double>::infinity()) == 0);
  REQUIRE(sourceEngineFromSelector(5.0) == 0);
  REQUIRE(sourceEngineFromSelector(31.0) == 0);
}

TEST_CASE("Each engine reports its display name") {
  REQUIRE(std::string(sourceEngineInfo(0).displayName) == "Macro Oscillator");
  REQUIRE(std::string(sourceEngineInfo(1).displayName) == "Digital Oscillator");
  REQUIRE(std::string(sourceEngineInfo(2).displayName) == "Modal Synth");
  REQUIRE(std::string(sourceEngineInfo(3).displayName) == "Resonator");
  REQUIRE(std::string(sourceEngineInfo(4).displayName) == "Audio Input");
  REQUIRE(std::string(sourceEngineInfo(9).displayName) == "Macro Oscillator");
}

TEST_CASE("makeAllSources builds every engine at its native rate") {
  const auto sources = makeAllSources();
  REQUIRE(sources.size() == size_t(kNumSourceEngines));
  for (int e = 0; e < kNumSourceEngines; ++e) {
    INFO("engine " << e);
    REQUIRE(sources[size_t(e)].engine != nullptr);
    REQUIRE(std::string(sources[size_t(e)].engine->info().id) == sourceEngineInfo(e).id);
  }
  REQUIRE(sources[0].nativeRate == 47872.34);
  REQUIRE(sources[1].nativeRate == 96000.0);
  REQUIRE(sources[2].nativeRate == 32000.0);
  REQUIRE(sources[3].nativeRate == 48000.0);
  REQUIRE(sources[4].nativeRate == 0.0);
  REQUIRE(sources[0].nativeBlock == 12);
  REQUIRE(sources[1].nativeBlock == 24);
  REQUIRE(sources[2].nativeBlock == 16);
  REQUIRE(sources[3].nativeBlock == 24);
  REQUIRE(sources[4].nativeBlock == 16);
}

TEST_CASE("Every engine fits in the Source slot's parameters") {
  for (int e = 0; e < kNumSourceEngines; ++e) {
    INFO(e);
    REQUIRE(sourceEngineInfo(e).paramCount <= int(kSourceParamCount));
    REQUIRE(sourceEngineInfo(e).paramCount <= kMaxSlotParams);
  }
}

TEST_CASE("Filter and Shaper engine ids are permanent") {
  REQUIRE(int(FilterEngine::Off) == 0);
  REQUIRE(int(FilterEngine::Analog) == 1);
  REQUIRE(int(FilterEngine::Classic) == 2);
  REQUIRE(int(FilterEngine::StateVariable) == 3);
  REQUIRE(kNumFilterEngines == 4);
  REQUIRE(kFilterEngineSelectorMax == 31);
  REQUIRE(int(ShaperEngine::Off) == 0);
  REQUIRE(int(ShaperEngine::RingMod) == 1);
  REQUIRE(int(ShaperEngine::Eq) == 2);
  REQUIRE(kNumShaperEngines == 3);
  REQUIRE(kShaperEngineSelectorMax == 31);
}

TEST_CASE("Filter and Shaper selectors fall back to Off") {
  REQUIRE(filterEngineFromSelector(2.0) == 2);
  REQUIRE(filterEngineFromSelector(4.0) == 0);
  REQUIRE(filterEngineFromSelector(std::numeric_limits<double>::quiet_NaN()) == 0);
  REQUIRE(shaperEngineFromSelector(1.4) == 1);
  REQUIRE(shaperEngineFromSelector(3.0) == 0);
  REQUIRE(shaperEngineFromSelector(-2.0) == 0);
}

TEST_CASE("Filter and Shaper engines report their display names") {
  const char* const filters[4] = {"Off", "Analog Filter", "Classic Filters", "State Variable Filter"};
  for (int e = 0; e < 4; ++e) REQUIRE(std::string(filterEngineInfo(e).displayName) == filters[e]);
  const char* const shapers[3] = {"Off", "Ring Mod / Vocoder", "EQ"};
  for (int e = 0; e < 3; ++e) REQUIRE(std::string(shaperEngineInfo(e).displayName) == shapers[e]);
  REQUIRE(std::string(filterEngineInfo(9).displayName) == "Off");
  for (int e = 0; e < 4; ++e) REQUIRE(filterEngineInfo(e).paramCount <= 4);   // spec §7.1: P1-P4
  for (int e = 0; e < 3; ++e) REQUIRE(shaperEngineInfo(e).paramCount <= 12);  // P1-P12 (amended)
  REQUIRE(shaperEngineInfo(int(ShaperEngine::RingMod)).sideInputs == 2);
}

TEST_CASE("Filter and Shaper factories build every engine at its rate") {
  const auto filters = makeAllFilters();
  REQUIRE(filters.size() == 4);
  for (int e = 0; e < 4; ++e) {
    REQUIRE(std::string(filters[size_t(e)].engine->info().id) == filterEngineInfo(e).id);
    REQUIRE(filters[size_t(e)].nativeRate == 0.0);  // host rate
    REQUIRE(filters[size_t(e)].engine->info().usesInput);
  }
  const auto shapers = makeAllShapers();
  REQUIRE(shapers.size() == 3);
  for (int e = 0; e < 3; ++e) {
    REQUIRE(std::string(shapers[size_t(e)].engine->info().id) == shaperEngineInfo(e).id);
    REQUIRE(shapers[size_t(e)].engine->info().usesInput);
  }
  REQUIRE(shapers[1].nativeRate == 96000.0);
  REQUIRE(shapers[1].nativeBlock == 60);
  REQUIRE(shapers[2].nativeRate == 0.0);
  REQUIRE(makeOffOnly().size() == 1);
}

TEST_CASE("Off passes its input through") {
  BypassEngine off;
  off.prepare(48000.0, 16);
  std::vector<float> l = {0.1f, -0.2f, 0.3f}, r = {0.4f, 0.5f, -0.6f}, ol(3), orr(3);
  const float* in[2] = {l.data(), r.data()};
  float* out[2] = {ol.data(), orr.data()};
  off.process(in, out, ParamValues{}, 3);
  REQUIRE(ol == l);
  REQUIRE(orr == r);
  off.process(nullptr, out, ParamValues{}, 3);
  for (float s : ol) REQUIRE(s == 0.0f);
}
