#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <string>

#include "engine/engine_registry.h"
#include "synth/param_table.h"
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
