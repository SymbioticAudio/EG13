#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "engine/bypass_engine.h"
#include "engine/engine_registry.h"
#include "engine/part_fx_engine.h"
#include "engine/ripples_engine.h"
#include "engine/shelves_engine.h"
#include "engine/svf_engine.h"
#include "rack.hpp"
#include "test_helpers.h"
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

// The filters' right-channel scratch holds prepare()'s maxBlock. A longer call (a broken caller)
// is split into maxBlock pieces instead of overrunning it, so it plays as those pieces would.
TEST_CASE("Stereo filters split a block longer than maxBlock instead of overrunning") {
  constexpr int kMax = 16;
  constexpr int kN = 40;
  std::vector<float> inL(kN), inR(kN);
  for (int i = 0; i < kN; ++i) {
    inL[size_t(i)] = std::sin(0.3f * float(i));
    inR[size_t(i)] = std::cos(0.2f * float(i));
  }
  // Renders kN samples in one call, or in calls of at most kMax.
  auto render = [&](VoiceEngine& e, bool pieces, std::vector<float>& l, std::vector<float>& r) {
    rack::random::seed(11);  // the Analog Filter's model draws Rack's random noise
    e.prepare(48000.0, kMax);
    e.setStereo(true);
    ParamValues p = test::defaultParams(e.info());
    std::vector<float> first(kMax);
    const float* in0[2] = {inL.data(), inR.data()};
    float* out0[2] = {first.data(), first.data()};
    e.process(in0, out0, p, kMax);  // snaps the controls; the step below then ramps per call
    p.p[0] = 0.3f;
    p.p[1] = 0.6f;
    l.assign(kN, 0.0f);
    r.assign(kN, 0.0f);
    const int step = pieces ? kMax : kN;
    for (int done = 0; done < kN; done += step) {
      const float* in[2] = {inL.data() + done, inR.data() + done};
      float* out[2] = {l.data() + done, r.data() + done};
      e.process(in, out, p, std::min(step, kN - done));
    }
  };
  RipplesEngine ripples;
  SvfEngine svf;
  ShelvesEngine shelves;
  for (VoiceEngine* e : {static_cast<VoiceEngine*>(&ripples), static_cast<VoiceEngine*>(&svf),
                         static_cast<VoiceEngine*>(&shelves)}) {
    INFO(e->info().displayName);
    std::vector<float> l1, r1, l2, r2;
    render(*e, false, l1, r1);
    render(*e, true, l2, r2);
    REQUIRE(l1 == l2);
    REQUIRE(r1 == r2);
    REQUIRE(r1 != l1);  // the right instance ran
  }
}

TEST_CASE("Part FX engine ids are permanent") {
  REQUIRE(int(PartFxEngine::Off) == 0);
  REQUIRE(int(PartFxEngine::Granular) == 1);
  REQUIRE(int(PartFxEngine::GranularDelay) == 2);
  REQUIRE(int(PartFxEngine::Resonator) == 3);
  REQUIRE(int(PartFxEngine::Reverb) == 4);
  REQUIRE(kNumPartFxEngines == 5);
  REQUIRE(kPartFxEngineSelectorMax == 31);
  REQUIRE(std::string(partFxEngineInfo(0).displayName) == "Off");
  REQUIRE(std::string(partFxEngineInfo(1).displayName) == "Granular");
  REQUIRE(std::string(partFxEngineInfo(2).displayName) == "Granular Delay");
  REQUIRE(std::string(partFxEngineInfo(3).displayName) == "Resonator");
  REQUIRE(std::string(partFxEngineInfo(4).displayName) == "Reverb");
  REQUIRE(std::string(partFxEngineInfo(9).displayName) == "Off");
  for (int e = 0; e <= 4; ++e) REQUIRE(partFxEngineFromSelector(double(e)) == e);
  REQUIRE(partFxEngineFromSelector(5.0) == 0);
  REQUIRE(partFxEngineFromSelector(17) == 0);
  REQUIRE(partFxEngineFromSelector(31.0) == 0);
  REQUIRE(partFxEngineFromSelector(-1.0) == 0);
  REQUIRE(partFxEngineFromSelector(std::numeric_limits<double>::quiet_NaN()) == 0);
}

TEST_CASE("Part FX trims are the measured ones (M5 Task 14, ruling G11)") {
  // Off and anything without an engine stay at unity; the others are measured in test_levels.cpp.
  for (int e : {-1, 0, 5, 7}) {
    REQUIRE(partFxTrimDb(e) == 0.0f);
    REQUIRE(partFxTrim(e) == 1.0f);
  }
  REQUIRE(partFxTrimDb(int(PartFxEngine::Granular)) == 5.0f);
  REQUIRE(partFxTrimDb(int(PartFxEngine::GranularDelay)) == 5.0f);
  REQUIRE(partFxTrimDb(int(PartFxEngine::Resonator)) == 0.5f);
  REQUIRE(partFxTrimDb(int(PartFxEngine::Reverb)) == 2.0f);
  REQUIRE(partFxTrim(int(PartFxEngine::Reverb)) == std::pow(10.0f, 2.0f / 20.0f));
}

TEST_CASE("makeAllPartFx builds every engine at its native rate and block") {
  const auto fx = makeAllPartFx();
  REQUIRE(fx.size() == size_t(kNumPartFxEngines));
  for (int e = 0; e < kNumPartFxEngines; ++e) {
    INFO("engine " << e);
    REQUIRE(fx[size_t(e)].engine != nullptr);
    REQUIRE(std::string(fx[size_t(e)].engine->info().id) == partFxEngineInfo(e).id);
    REQUIRE(fx[size_t(e)].wholeBlocks);  // ruling G8
  }
  REQUIRE(fx[0].nativeRate == 0.0);  // Off: the existing makeOff()
  REQUIRE(fx[0].nativeBlock == makeOff().nativeBlock);
  REQUIRE(fx[1].nativeRate == 32000.0);
  REQUIRE(fx[1].nativeBlock == 32);
  // Granular Delay runs at the host rate and nests its own converters; G17: its block is kHostBlock.
  REQUIRE(fx[2].nativeRate == 0.0);
  REQUIRE(fx[2].nativeBlock == makeOff().nativeBlock);
  REQUIRE(fx[2].nativeBlock == makeFilter(int(FilterEngine::Analog)).nativeBlock);
  REQUIRE(fx[3].nativeRate == 48000.0);
  REQUIRE(fx[3].nativeBlock == 24);
  REQUIRE(fx[4].nativeRate == 32000.0);
  REQUIRE(fx[4].nativeBlock == 16);
  REQUIRE(makePartFx(9).nativeRate == 0.0);  // unknown -> Off
  REQUIRE(std::string(makePartFx(9).engine->info().id) == partFxEngineInfo(0).id);
  REQUIRE_FALSE(makeFilter(1).wholeBlocks);  // other slots are unaffected
}

TEST_CASE("Every Part FX engine's defaults equal the per-position host defaults") {
  for (int e = 1; e < kNumPartFxEngines; ++e) {
    const EngineInfo& info = partFxEngineInfo(e);
    INFO(info.displayName);
    REQUIRE(info.paramCount <= kPartFxParamCount);
    REQUIRE(info.paramCount > 0);
    for (int i = 0; i < info.paramCount; ++i) {
      INFO("P" << i + 1);
      REQUIRE(info.params[i].defaultValue == kPartFxHostDefaults[i]);
    }
  }
}
