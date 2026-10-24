#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <algorithm>
#include <iterator>
#include <limits>
#include <set>
#include <string>

#include "engine/audio_input_engine.h"
#include "engine/engine_registry.h"
#include "engine/ripples_engine.h"
#include "engine/tides_engine.h"
#include "mod/mod_factory.h"
#include "params/param_ids.h"
#include "synth/param_table.h"

using namespace eg13;

namespace {

std::string text(size_t index, double value) {
  char buf[64];
  formatParam(index, value, buf, sizeof(buf));
  return buf;
}

}  // namespace

TEST_CASE("The table has 323 parameters with unique IDs") {
  REQUIRE(kSourceParamCount == 16);
  REQUIRE(kFilterParamCount == 4);
  REQUIRE(kShaperParamCount == 12);
  REQUIRE(kModSlotParamCount == 16);
  REQUIRE(kParamsPerPart == 158);
  REQUIRE(kParamCount == 323);
  std::set<uint32_t> ids;
  for (size_t i = 0; i < kParamCount; ++i) ids.insert(paramSpec(i).id);
  REQUIRE(ids.size() == kParamCount);
  for (size_t i = 0; i < kParamCount; ++i) REQUIRE(paramIndex(paramSpec(i).id) == int(i));
  REQUIRE(paramIndex(0xDEADBEEF) == -1);
}

TEST_CASE("M0 parameter IDs keep their meaning and range") {
  const int model = paramIndex(params::id(0, params::Slot::Source, 1));
  REQUIRE(model == int(sourceParamIndex(0, 0)));
  REQUIRE(std::string(paramSpec(size_t(model)).name) == "Model");
  REQUIRE(paramSpec(size_t(model)).minValue == 0.0f);
  REQUIRE(paramSpec(size_t(model)).maxValue == 1.0f);
  REQUIRE(paramIndex(params::id(0, params::Slot::Source, 8)) == int(sourceParamIndex(0, 7)));
  const size_t level = masterParamIndex(MasterParam::Level);
  REQUIRE(paramSpec(level).id == params::kMasterLevel);
  REQUIRE(paramSpec(level).defaultValue == 0.8f);
}

TEST_CASE("Index helpers agree with parameter IDs") {
  REQUIRE(paramSpec(sourceParamIndex(1, 2)).id == params::id(1, params::Slot::Source, 3));
  REQUIRE(paramSpec(partParamIndex(0, PartParam::Level)).id == params::id(0, params::Slot::Part, 1));
  REQUIRE(paramSpec(partParamIndex(1, PartParam::Output)).id ==
          params::id(1, params::Slot::Part, 14));
  REQUIRE(paramSpec(masterParamIndex(MasterParam::SplitNote)).id ==
          params::id(params::kMasterPart, params::Slot::Part, 3));
  REQUIRE(std::string(paramSpec(partParamIndex(1, PartParam::MidiChannel)).name) == "MIDI Channel");
  REQUIRE(std::string(paramSpec(partParamIndex(1, PartParam::MidiChannel)).module) == "Part B");
  REQUIRE(std::string(paramSpec(sourceParamIndex(1, 0)).module) == "Part B/Source");
}

TEST_CASE("EG13 FX defaults form its init patch") {
  REQUIRE(paramDefault(masterParamIndex(MasterParam::Dry), PluginKind::Effect) == 0.0f);
  REQUIRE(paramDefault(masterParamIndex(MasterParam::Level), PluginKind::Effect) == 1.0f);
  REQUIRE(paramDefault(masterParamIndex(MasterParam::Level), PluginKind::Instrument) == 0.8f);
  REQUIRE(paramDefault(sourceEngineParamIndex(0), PluginKind::Effect) == 4.0f);
  REQUIRE(paramDefault(sourceEngineParamIndex(1), PluginKind::Effect) == 0.0f);
  REQUIRE(paramDefault(sourceEngineParamIndex(0), PluginKind::Instrument) == 0.0f);
  REQUIRE(paramDefault(partParamIndex(0, PartParam::Trigger), PluginKind::Effect) == 1.0f);
  REQUIRE(paramDefault(partParamIndex(1, PartParam::Trigger), PluginKind::Effect) == 0.0f);
  REQUIRE(paramDefault(sourceParamIndex(0, 0), PluginKind::Effect) ==
          AudioInputEngine::staticInfo().params[0].defaultValue);
  REQUIRE(paramDefault(partParamIndex(0, PartParam::VoiceCount), PluginKind::Effect) == 8.0f);
}

TEST_CASE("clampParam clamps, rounds stepped values and maps NaN to the minimum") {
  const size_t voices = partParamIndex(0, PartParam::VoiceCount);
  REQUIRE(clampParam(voices, 3.6) == 4.0f);
  REQUIRE(clampParam(voices, 0.0) == 1.0f);
  REQUIRE(clampParam(voices, 99.0) == 8.0f);
  REQUIRE(clampParam(voices, std::numeric_limits<double>::quiet_NaN()) == 1.0f);
  const size_t pan = partParamIndex(0, PartParam::Pan);
  REQUIRE(clampParam(pan, -0.25) == -0.25f);
  REQUIRE(clampParam(pan, -3.0) == -1.0f);
  REQUIRE(clampParam(sourceParamIndex(0, 1), 1.5) == 1.0f);
}

TEST_CASE("Parameters format readable values") {
  REQUIRE(text(partParamIndex(0, PartParam::BaseNote), 60) == "C4");
  REQUIRE(text(partParamIndex(0, PartParam::BaseNote), 61) == "C#4");
  REQUIRE(text(partParamIndex(0, PartParam::KeyLow), 0) == "C-1");
  REQUIRE(text(partParamIndex(0, PartParam::MidiChannel), 0) == "Omni");
  REQUIRE(text(partParamIndex(0, PartParam::MidiChannel), 10) == "10");
  REQUIRE(text(partParamIndex(0, PartParam::Pan), -0.5) == "L50");
  REQUIRE(text(partParamIndex(0, PartParam::Pan), 0.0) == "C");
  REQUIRE(text(partParamIndex(0, PartParam::Pan), 1.0) == "R100");
  REQUIRE(text(partParamIndex(0, PartParam::Glide), 0.0) == "Off");
  REQUIRE(text(partParamIndex(0, PartParam::Glide), 0.5) == "500 ms");
  REQUIRE(text(partParamIndex(0, PartParam::Glide), 1.0) == "2.00 s");
  REQUIRE(text(partParamIndex(0, PartParam::Transpose), 3) == "+3 st");
  REQUIRE(text(partParamIndex(0, PartParam::BendRange), 2) == "+/-2 st");
  REQUIRE(text(partParamIndex(0, PartParam::VoiceMode), 2) == "Legato");
  REQUIRE(text(partParamIndex(0, PartParam::VoiceCount), 4) == "4");
  REQUIRE(text(partParamIndex(0, PartParam::Trigger), 1) == "Always on");
  REQUIRE(text(partParamIndex(0, PartParam::Mono), 1) == "On");
  REQUIRE(text(partParamIndex(0, PartParam::Output), 1) == "Bus 2");
  REQUIRE(text(partParamIndex(0, PartParam::Output), 2) == "Other part");
  REQUIRE(clampParam(partParamIndex(0, PartParam::Output), 5) == 2.0f);
  REQUIRE(text(masterParamIndex(MasterParam::Mode), 2) == "Split");
  REQUIRE(text(masterParamIndex(MasterParam::Dry), 0.25) == "25%");
  REQUIRE(text(sourceParamIndex(1, 0), (8.0 + 0.5) / 24.0) == "Virtual analog");
}

TEST_CASE("glideSeconds maps 0..1 to 0..2 s quadratically") {
  REQUIRE(glideSeconds(0.0f) == 0.0f);
  REQUIRE(glideSeconds(0.5f) == 0.5f);
  REQUIRE(glideSeconds(1.0f) == 2.0f);
}

TEST_CASE("Engine selectors and Input source have permanent IDs") {
  REQUIRE(paramSpec(sourceEngineParamIndex(0)).id == params::id(0, params::Slot::Source, 0));
  REQUIRE(paramSpec(sourceEngineParamIndex(1)).id == params::id(1, params::Slot::Source, 0));
  REQUIRE(paramSpec(sourceEngineParamIndex(0)).maxValue == 31.0f);
  REQUIRE(paramSpec(sourceEngineParamIndex(0)).stepped);
  REQUIRE(std::string(paramSpec(sourceEngineParamIndex(0)).name) == "Engine");
  REQUIRE(paramSpec(partParamIndex(1, PartParam::InputSource)).id ==
          params::id(1, params::Slot::Part, 15));
  REQUIRE(paramSpec(sourceParamIndex(0, 0)).id == params::id(0, params::Slot::Source, 1));
  REQUIRE(paramSpec(partParamIndex(0, PartParam::Level)).id == params::id(0, params::Slot::Part, 1));
}

TEST_CASE("Source P9..P16 have their own IDs after P1..P8") {
  for (int part = 0; part < kNumParts; ++part) {
    for (int p = 8; p < 16; ++p) {
      const uint32_t id = params::id(uint32_t(part), params::Slot::Source, uint32_t(p + 1));
      REQUIRE(paramSpec(sourceParamIndex(part, p)).id == id);
      REQUIRE(paramIndex(id) == int(sourceParamIndex(part, p)));
      REQUIRE(paramSpec(sourceParamIndex(part, p)).minValue == 0.0f);
      REQUIRE(paramSpec(sourceParamIndex(part, p)).maxValue == 1.0f);
    }
  }
  // Layout per part: Source selector + P1..P16, Filter + P1..P4, Shaper + P1..P12, four mod slots
  // (selector + P1..P16 + Scope), Part FX (selector + P1..P16), 12 route amounts, part settings.
  REQUIRE(filterEngineParamIndex(0) == 17);
  REQUIRE(shaperEngineParamIndex(0) == 22);
  REQUIRE(modEngineParamIndex(0, 0) == 35);
  REQUIRE(modScopeParamIndex(0, 0) == 52);
  REQUIRE(modEngineParamIndex(0, 3) == 89);
  REQUIRE(partFxEngineParamIndex(0) == 107);
  REQUIRE(partFxParamIndex(0, 15) == 123);
  REQUIRE(routeAmountParamIndex(0, 0) == 124);
  REQUIRE(partParamIndex(0, PartParam::Level) == 136);
  REQUIRE(partParamIndex(0, PartParam::PartFxInput) == 157);
  REQUIRE(sourceEngineParamIndex(1) == 158);
  REQUIRE(masterParamIndex(MasterParam::Level) == 316);
}

TEST_CASE("Engine selector and Input source format readable values") {
  REQUIRE(text(sourceEngineParamIndex(0), 0) == "Macro Oscillator");
  REQUIRE(text(sourceEngineParamIndex(0), 2) == "Modal Synth");
  REQUIRE(text(sourceEngineParamIndex(0), 4) == "Audio Input");
  REQUIRE(text(sourceEngineParamIndex(0), 20) == "Macro Oscillator");
  REQUIRE(text(partParamIndex(0, PartParam::InputSource), 0) == "External");
  REQUIRE(text(partParamIndex(0, PartParam::InputSource), 1) == "Other part");
  REQUIRE(text(partParamIndex(0, PartParam::InputSource), 2) == "Both");
}

TEST_CASE("Source parameters format and name through the selected engine") {
  char buf[64];
  formatParam(sourceParamIndex(0, 0), 0.5 / 47.0, buf, sizeof(buf), 1);
  REQUIRE(std::string(buf) == "CSAW");
  REQUIRE(std::string(paramName(sourceParamIndex(0, 0), 1)) == "Shape");
  REQUIRE(std::string(paramName(sourceParamIndex(0, 0), 0)) == "Model");
  REQUIRE(std::string(paramName(sourceParamIndex(0, 7), 1)) == "(unused)");
  formatParam(sourceParamIndex(0, 7), 0.5, buf, sizeof(buf), 1);
  REQUIRE(std::string(buf) == "-");
  REQUIRE(std::string(paramName(masterParamIndex(MasterParam::Dry), 3)) == "Dry");
  // Slot 16 on the Macro Oscillator (8 params) is unused.
  REQUIRE(std::string(paramName(sourceParamIndex(0, 15), 0)) == "(unused)");
  formatParam(sourceParamIndex(0, 15), 0.5, buf, sizeof(buf), 0);
  REQUIRE(std::string(buf) == "-");
  REQUIRE(std::string(paramName(sourceParamIndex(1, 15), 0)) == "(unused)");
  REQUIRE(std::string(paramSpec(sourceParamIndex(1, 15)).name) == "(unused)");
  REQUIRE(paramDefault(sourceParamIndex(1, 15), PluginKind::Instrument) == 0.0f);
  REQUIRE(paramDefault(sourceParamIndex(0, 15), PluginKind::Effect) == 0.0f);
}

TEST_CASE("Filter and Shaper parameters have permanent IDs") {
  for (int part = 0; part < kNumParts; ++part) {
    const uint32_t p = uint32_t(part);
    REQUIRE(paramSpec(filterEngineParamIndex(part)).id == params::id(p, params::Slot::Filter, 0));
    REQUIRE(paramSpec(shaperEngineParamIndex(part)).id == params::id(p, params::Slot::Shaper, 0));
    for (int i = 0; i < int(kFilterParamCount); ++i) {
      REQUIRE(paramSpec(filterParamIndex(part, i)).id == params::id(p, params::Slot::Filter, uint32_t(i + 1)));
      REQUIRE(paramSpec(filterParamIndex(part, i)).maxValue == 1.0f);
      REQUIRE_FALSE(paramSpec(filterParamIndex(part, i)).stepped);
    }
    for (int i = 0; i < int(kShaperParamCount); ++i) {
      REQUIRE(paramSpec(shaperParamIndex(part, i)).id == params::id(p, params::Slot::Shaper, uint32_t(i + 1)));
    }
    for (size_t selector : {filterEngineParamIndex(part), shaperEngineParamIndex(part)}) {
      REQUIRE(paramSpec(selector).stepped);
      REQUIRE(paramSpec(selector).minValue == 0.0f);
      REQUIRE(paramSpec(selector).maxValue == 31.0f);
      REQUIRE(std::string(paramSpec(selector).name) == "Type");
    }
  }
  REQUIRE(std::string(paramSpec(filterParamIndex(1, 0)).module) == "Part B/Filter");
  REQUIRE(std::string(paramSpec(shaperParamIndex(0, 11)).module) == "Part A/Shaper");
  // Source IDs are unchanged.
  REQUIRE(paramSpec(sourceParamIndex(0, 0)).id == params::id(0, params::Slot::Source, 1));
  REQUIRE(paramSpec(sourceParamIndex(1, 15)).id == params::id(1, params::Slot::Source, 16));
}

TEST_CASE("Filter and Shaper defaults per plugin") {
  for (int part = 0; part < kNumParts; ++part) {
    // The instrument init patch's filter (M4, provisional ruling 28).
    REQUIRE(paramDefault(filterEngineParamIndex(part), PluginKind::Instrument) == float(FilterEngine::StateVariable));
    REQUIRE(paramDefault(shaperEngineParamIndex(part), PluginKind::Instrument) == 0.0f);
    REQUIRE(paramDefault(shaperEngineParamIndex(part), PluginKind::Effect) == 0.0f);
  }
  REQUIRE(paramDefault(filterEngineParamIndex(0), PluginKind::Effect) == float(FilterEngine::Analog));
  REQUIRE(paramDefault(filterEngineParamIndex(1), PluginKind::Effect) == 0.0f);
  REQUIRE(paramDefault(filterParamIndex(0, 0), PluginKind::Effect) ==
          RipplesEngine::staticInfo().params[0].defaultValue);
  // Filter/Shaper P host defaults are one value per position, whatever the default engine
  // (final review I1): Off ignores them, the other engines get a sensible reset.
  REQUIRE(paramDefault(filterParamIndex(0, 0), PluginKind::Instrument) == 0.7f);  // the init patch's cutoff
  REQUIRE(paramDefault(filterParamIndex(1, 0), PluginKind::Effect) == 1.0f);
  REQUIRE(paramDefault(filterParamIndex(1, 1), PluginKind::Instrument) == 0.0f);
  REQUIRE(paramDefault(shaperParamIndex(0, 1), PluginKind::Instrument) == 0.5f);  // EQ gain 0 dB
  REQUIRE(paramDefault(shaperParamIndex(1, 3), PluginKind::Effect) == 0.5f);
  REQUIRE(std::string(paramSpec(filterParamIndex(0, 0)).name) == "Cutoff");  // the SVF's
}

TEST_CASE("Filter and Shaper parameters name and format through their engines") {
  char buf[64];
  const PartEngines analog{0, int(FilterEngine::Analog), int(ShaperEngine::Eq)};
  REQUIRE(std::string(paramName(filterParamIndex(0, 0), analog)) == "Frequency");
  formatParam(filterParamIndex(0, 0), 1.0, buf, sizeof(buf), analog);
  REQUIRE(std::string(buf) == "20.00 kHz");
  REQUIRE(std::string(paramName(filterParamIndex(0, 3), analog)) == "(unused)");
  formatParam(filterParamIndex(0, 3), 0.5, buf, sizeof(buf), analog);
  REQUIRE(std::string(buf) == "-");
  REQUIRE(std::string(paramName(shaperParamIndex(1, 11), analog)) == "Output");
  formatParam(shaperParamIndex(1, 11), 0.5 / 7.0, buf, sizeof(buf), analog);
  REQUIRE(std::string(buf) == "EQ");
  const PartEngines off;
  REQUIRE(std::string(paramName(shaperParamIndex(0, 0), off)) == "(unused)");
  REQUIRE(std::string(paramName(sourceParamIndex(0, 0), off)) == "Model");
  formatParam(filterEngineParamIndex(0), 1, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Analog Filter");
  formatParam(filterEngineParamIndex(0), 3, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "State Variable Filter");
  formatParam(filterEngineParamIndex(0), 17, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Off");
  formatParam(shaperEngineParamIndex(0), 1, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Ring Mod / Vocoder");
}

TEST_CASE("Slot lookups find every slot parameter and selector") {
  int counted[kNumSlotKinds] = {};
  for (size_t i = 0; i < kParamCount; ++i) {
    SlotKind kind = SlotKind::Source;
    int part = -1, p = -1;
    if (slotParamOf(i, &kind, &part, &p)) {
      REQUIRE(slotParamIndex(kind, part, p) == i);
      ++counted[int(kind)];
    }
    if (slotSelectorOf(i, &kind, &part)) REQUIRE(slotEngineParamIndex(kind, part) == i);
  }
  REQUIRE(counted[0] == 2 * 16);
  REQUIRE(counted[1] == 2 * 4);
  REQUIRE(counted[2] == 2 * 12);
  for (int k = int(SlotKind::Mod1); k <= int(SlotKind::Mod4); ++k) REQUIRE(counted[k] == 2 * 16);
  REQUIRE(counted[int(SlotKind::PartFx)] == 2 * 16);
  REQUIRE(slotEngineCount(SlotKind::PartFx) == 5);
  REQUIRE(slotEngineFromSelector(SlotKind::PartFx, 3.0) == 3);
  REQUIRE(slotEngineFromSelector(SlotKind::PartFx, 9.0) == 0);
  REQUIRE_FALSE(isModSlot(SlotKind::PartFx));
  REQUIRE(isModSlot(SlotKind::Mod4));
  REQUIRE_FALSE(isModSlot(SlotKind::Shaper));
  for (int e = 0; e < slotEngineCount(SlotKind::Filter); ++e) {
    REQUIRE(slotEngineInfo(SlotKind::Filter, e).paramCount <= int(kFilterParamCount));
  }
  for (int e = 0; e < slotEngineCount(SlotKind::Shaper); ++e) {
    REQUIRE(slotEngineInfo(SlotKind::Shaper, e).paramCount <= int(kShaperParamCount));
  }
  REQUIRE(slotEngineFromSelector(SlotKind::Shaper, 2.0) == 2);
  REQUIRE(slotEngineFromSelector(SlotKind::Filter, 9.0) == 0);
}

TEST_CASE("Mod slot, scope, route and new setting IDs are stable") {
  REQUIRE(paramSpec(modEngineParamIndex(0, 0)).id == params::id(0, params::Slot::Mod1, 0));
  REQUIRE(paramSpec(modParamIndex(0, 0, 0)).id == params::id(0, params::Slot::Mod1, 1));
  REQUIRE(paramSpec(modParamIndex(1, 3, 15)).id == params::id(1, params::Slot::Mod4, 16));
  REQUIRE(paramSpec(modScopeParamIndex(0, 2)).id == params::id(0, params::Slot::Mod3, 17));
  REQUIRE(paramSpec(routeAmountParamIndex(0, 0)).id == params::id(0, params::Slot::Matrix, 1));
  REQUIRE(paramSpec(routeAmountParamIndex(1, 11)).id == params::id(1, params::Slot::Matrix, 12));
  REQUIRE(paramSpec(partParamIndex(0, PartParam::MpeMode)).id == params::id(0, params::Slot::Part, 16));
  REQUIRE(paramSpec(partParamIndex(1, PartParam::CcD)).id == params::id(1, params::Slot::Part, 21));
  REQUIRE(paramSpec(masterParamIndex(MasterParam::VoiceGain)).id ==
          params::id(params::kMasterPart, params::Slot::Part, 4));
  REQUIRE(paramSpec(masterParamIndex(MasterParam::SoftClip)).id ==
          params::id(params::kMasterPart, params::Slot::Part, 5));
  // Earlier IDs keep their entries.
  REQUIRE(paramIndex(params::id(0, params::Slot::Shaper, 12)) == int(shaperParamIndex(0, 11)));
  REQUIRE(paramIndex(params::id(1, params::Slot::Part, 15)) == int(partParamIndex(1, PartParam::InputSource)));
  REQUIRE(std::string(paramSpec(modEngineParamIndex(1, 1)).module) == "Part B/Mod 2");
  REQUIRE(std::string(paramSpec(routeAmountParamIndex(0, 4)).module) == "Part A/Matrix");
  REQUIRE(std::string(paramSpec(routeAmountParamIndex(0, 4)).name) == "Route 5");
  REQUIRE(std::string(paramSpec(modScopeParamIndex(0, 0)).name) == "Scope");
  int part = -1, slot = -1;
  REQUIRE(modScopeOf(modScopeParamIndex(1, 3), &part, &slot));
  REQUIRE(part == 1);
  REQUIRE(slot == 3);
  REQUIRE_FALSE(modScopeOf(modParamIndex(1, 3, 15), &part, &slot));
  int route = -1;
  REQUIRE(routeAmountOf(routeAmountParamIndex(1, 7), &part, &route));
  REQUIRE(route == 7);
  REQUIRE_FALSE(routeAmountOf(partParamIndex(0, PartParam::Level), &part, &route));
}

TEST_CASE("The init patches set mod engines, scopes, routes and amounts") {
  for (int part = 0; part < kNumParts; ++part) {
    REQUIRE(paramDefault(modEngineParamIndex(part, 0), PluginKind::Instrument) == float(ModEngineKind::Envelope));
    REQUIRE(paramDefault(modEngineParamIndex(part, 1), PluginKind::Instrument) == float(ModEngineKind::Envelope));
    REQUIRE(paramDefault(modEngineParamIndex(part, 2), PluginKind::Instrument) == float(ModEngineKind::Lfo));
    REQUIRE(paramDefault(modEngineParamIndex(part, 3), PluginKind::Instrument) == 0.0f);
    REQUIRE(paramDefault(modScopeParamIndex(part, 0), PluginKind::Instrument) == 0.0f);
    REQUIRE(initPatchRoute(PluginKind::Instrument, part, 0) == Route{modOutputSource(0, 0), kDestAmp, kSrcVelocity});
    REQUIRE(initPatchRoute(PluginKind::Instrument, part, 1) == Route{modOutputSource(1, 0), kDestFilterP, kSrcNone});
    REQUIRE(initPatchRoute(PluginKind::Instrument, part, 2) == Route{modOutputSource(2, 0), kDestPitch, kSrcModWheel});
    REQUIRE(initPatchRoute(PluginKind::Instrument, part, 3) == Route{});
    REQUIRE(paramDefault(routeAmountParamIndex(part, 0), PluginKind::Instrument) == 1.0f);
    REQUIRE(paramDefault(routeAmountParamIndex(part, 1), PluginKind::Instrument) == 0.3f);
    REQUIRE(paramDefault(routeAmountParamIndex(part, 2), PluginKind::Instrument) * 24.0f == 0.5f);
    REQUIRE(paramDefault(routeAmountParamIndex(part, 3), PluginKind::Instrument) == 0.0f);
  }
  REQUIRE(paramDefault(modEngineParamIndex(0, 0), PluginKind::Effect) == float(ModEngineKind::Lfo));
  REQUIRE(paramDefault(modScopeParamIndex(0, 0), PluginKind::Effect) == 1.0f);  // per part
  REQUIRE(paramDefault(modEngineParamIndex(1, 0), PluginKind::Effect) == 0.0f);
  REQUIRE(initPatchRoute(PluginKind::Effect, 0, 0) == Route{modOutputSource(0, 0), kDestFilterP, kSrcNone});
  REQUIRE(initPatchRoute(PluginKind::Effect, 1, 0) == Route{});
  REQUIRE(paramDefault(routeAmountParamIndex(0, 0), PluginKind::Effect) == 0.2f);
  REQUIRE(paramDefault(routeAmountParamIndex(1, 0), PluginKind::Effect) == 0.0f);
  // The init LFO rates (provisional ruling 29).
  REQUIRE(std::fabs(paramDefault(modParamIndex(0, 2, 2), PluginKind::Instrument) -
                    TidesEngine::frequencyFor(5.0, TidesEngine::Medium)) < 1e-6f);
  REQUIRE(std::fabs(paramDefault(modParamIndex(0, 0, 2), PluginKind::Effect) -
                    TidesEngine::frequencyFor(0.25, TidesEngine::Medium)) < 1e-6f);
  // Voice Gain -12 dB in the instrument, 0 dB in EG13 FX; Soft Clip on in both.
  REQUIRE(paramDefault(masterParamIndex(MasterParam::VoiceGain), PluginKind::Instrument) == 0.0f);
  REQUIRE(paramDefault(masterParamIndex(MasterParam::VoiceGain), PluginKind::Effect) == 1.0f);
  REQUIRE(paramDefault(masterParamIndex(MasterParam::SoftClip), PluginKind::Instrument) == 1.0f);
  REQUIRE(paramDefault(partParamIndex(0, PartParam::MpeBendRange), PluginKind::Instrument) == 48.0f);
  REQUIRE(paramDefault(partParamIndex(0, PartParam::CcA), PluginKind::Instrument) == 2.0f);
  REQUIRE(paramDefault(partParamIndex(0, PartParam::CcD), PluginKind::Instrument) == 16.0f);
}

TEST_CASE("Mod slot host defaults suit every mod engine") {
  // A host's reset sets each mod P to one value whatever engine is selected (provisional ruling
  // 1). Every engine must play sensibly there, and away from the documented init-patch rates
  // they are the engines' own defaults, so a reset and a fresh engine agree.
  for (int e = 1; e < kNumModEngines; ++e) {
    const EngineInfo& info = modEngineInfo(e).engine;
    for (int p = 0; p < info.paramCount; ++p) {
      INFO("engine " << info.displayName << " P" << p + 1);
      REQUIRE(paramDefault(modParamIndex(1, 3, p), PluginKind::Instrument) == info.params[p].defaultValue);
    }
    for (PluginKind plugin : {PluginKind::Instrument, PluginKind::Effect}) {
      for (int slot = 0; slot < kNumModSlots; ++slot) {
        ParamValues p;
        for (int i = 0; i < kModParamCount; ++i) p.p[i] = paramDefault(modParamIndex(0, slot, i), plugin);
        auto engine = makeModEngine(e);
        engine->prepare(48000.0);
        engine->reset();
        float lowest[kMaxModOutputs], highest[kMaxModOutputs];
        std::fill(std::begin(lowest), std::end(lowest), 2.0f);
        std::fill(std::begin(highest), std::end(highest), -2.0f);
        float out[kMaxModOutputs];
        for (int t = 0; t < 9000; ++t) {  // 3 s, the gate toggling every half second
          ModContext ctx;
          ctx.gate = (t / 1500) % 2 == 0;
          ctx.beat = double(t) * 16.0 * 2.0 / 48000.0;
          ctx.beatsPerSample = 2.0 / 48000.0;
          engine->process(ctx, p, 16, out);
          for (int k = 0; k < kMaxModOutputs; ++k) {
            REQUIRE(std::isfinite(out[k]));
            lowest[k] = std::min(lowest[k], out[k]);
            highest[k] = std::max(highest[k], out[k]);
          }
        }
        INFO("engine " << info.displayName << " slot " << slot);
        REQUIRE(highest[0] - lowest[0] > 0.1f);  // output 1 moves
        for (int k = 0; k < kMaxModOutputs; ++k) {
          REQUIRE(lowest[k] >= -1.0f);
          REQUIRE(highest[k] <= 1.0f);
        }
      }
    }
  }
}

TEST_CASE("Mod slot and route parameters name and format through their engines and destinations") {
  char buf[64];
  PartEngines engines;
  engines.mod[0] = int(ModEngineKind::Lfo);
  engines.mod[1] = int(ModEngineKind::Envelope);
  engines.routeDestination[0] = kDestPitch;
  engines.routeDestination[1] = kDestFilterP;
  REQUIRE(std::string(paramName(modParamIndex(0, 0, 1), engines)) == "Sync");
  REQUIRE(std::string(paramName(modParamIndex(0, 0, 7), engines)) == "(unused)");
  REQUIRE(std::string(paramName(modParamIndex(0, 1, 1), engines)) == "Attack");
  REQUIRE(std::string(paramName(modParamIndex(0, 3, 0), engines)) == "(unused)");  // Off
  formatParam(modParamIndex(0, 1, 1), 0.0, buf, sizeof(buf), engines);
  REQUIRE(std::string(buf) == "1 ms");
  formatParam(modParamIndex(0, 0, 7), 0.5, buf, sizeof(buf), engines);
  REQUIRE(std::string(buf) == "-");
  formatParam(modEngineParamIndex(0, 2), 3, buf, sizeof(buf), engines);
  REQUIRE(std::string(buf) == "Random / S&H");
  formatParam(modEngineParamIndex(0, 2), 25, buf, sizeof(buf), engines);
  REQUIRE(std::string(buf) == "Off");
  formatParam(modScopeParamIndex(0, 2), 1, buf, sizeof(buf), engines);
  REQUIRE(std::string(buf) == "Per part");
  formatParam(routeAmountParamIndex(0, 0), 0.5 / 24.0, buf, sizeof(buf), engines);
  REQUIRE(std::string(buf) == "+0.50 st");
  formatParam(routeAmountParamIndex(0, 1), -0.3, buf, sizeof(buf), engines);
  REQUIRE(std::string(buf) == "-30%");
  formatParam(routeAmountParamIndex(0, 2), 0.3, buf, sizeof(buf), engines);
  REQUIRE(std::string(buf) == "-");  // an empty route
  formatParam(partParamIndex(0, PartParam::MpeMode), 2, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Upper zone");
  formatParam(partParamIndex(0, PartParam::CcB), 74, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "CC 74");
  formatParam(partParamIndex(0, PartParam::MpeBendRange), 48, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "+/-48 st");
  formatParam(masterParamIndex(MasterParam::VoiceGain), 0, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "-12 dB");
  formatParam(masterParamIndex(MasterParam::SoftClip), 1, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "On");
  REQUIRE(clampParam(routeAmountParamIndex(0, 0), -3.0) == -1.0f);
  REQUIRE(clampParam(modScopeParamIndex(0, 0), 0.6) == 1.0f);
}

namespace {

struct BaseSpec {
  uint32_t id;
  const char* name;
  const char* module;
  float minValue, maxValue, defaultValue, effectDefault;
  bool stepped;
  int format;
};

// The table at BASE ede0d80 (before Part FX), captured from the table itself.
const BaseSpec kBaseSpecs[] = {
#include "param_table_base.inc"
};

}  // namespace

TEST_CASE("Every existing parameter ID keeps its spec") {
  REQUIRE(sizeof(kBaseSpecs) / sizeof(kBaseSpecs[0]) == 287);
  for (const BaseSpec& base : kBaseSpecs) {
    const int index = paramIndex(base.id);
    INFO("id 0x" << std::hex << base.id << " " << base.name);
    REQUIRE(index >= 0);
    const ParamSpec& now = paramSpec(size_t(index));
    REQUIRE(now.id == base.id);
    REQUIRE(std::string(now.name) == base.name);
    REQUIRE(std::string(now.module) == base.module);
    REQUIRE(now.minValue == base.minValue);
    REQUIRE(now.maxValue == base.maxValue);
    REQUIRE(now.defaultValue == base.defaultValue);
    REQUIRE(now.effectDefault == base.effectDefault);
    REQUIRE(now.stepped == base.stepped);
    REQUIRE(int(now.format) == base.format);
  }
}

TEST_CASE("Part FX parameter IDs") {
  REQUIRE(paramIndex(params::id(0, params::Slot::PartFx, 0)) == int(partFxEngineParamIndex(0)));
  REQUIRE(paramIndex(params::id(0, params::Slot::PartFx, 16)) == int(partFxParamIndex(0, 15)));
  REQUIRE(paramIndex(params::id(1, params::Slot::PartFx, 16)) == int(partFxParamIndex(1, 15)));
  REQUIRE(paramIndex(params::id(0, params::Slot::PartFx, 17)) == -1);
  REQUIRE(paramIndex(params::id(0, params::Slot::Part, 22)) == int(partParamIndex(0, PartParam::PartFxInput)));
  REQUIRE(uint32_t(PartParam::PartFxInput) == 22);
  REQUIRE(kPartParamCount == 22);
  const ParamSpec& type = paramSpec(partFxEngineParamIndex(1));
  REQUIRE(type.stepped);
  REQUIRE(type.minValue == 0.0f);
  REQUIRE(type.maxValue == 31.0f);
  for (int p = 0; p < 16; ++p) {
    const ParamSpec& s = paramSpec(partFxParamIndex(0, p));
    REQUIRE_FALSE(s.stepped);
    REQUIRE(s.minValue == 0.0f);
    REQUIRE(s.maxValue == 1.0f);
    REQUIRE(s.defaultValue == kPartFxHostDefaults[p]);
    REQUIRE(s.effectDefault == kPartFxHostDefaults[p]);
  }
  const ParamSpec& input = paramSpec(partParamIndex(0, PartParam::PartFxInput));
  REQUIRE(input.stepped);
  REQUIRE(input.maxValue == 2.0f);
  REQUIRE(input.defaultValue == 0.0f);
  REQUIRE(input.effectDefault == 0.0f);
  REQUIRE(text(partParamIndex(0, PartParam::PartFxInput), 0) == "Part mix");
  REQUIRE(text(partParamIndex(0, PartParam::PartFxInput), 1) == "Input");
  REQUIRE(text(partParamIndex(0, PartParam::PartFxInput), 2) == "Part mix + Input");
}

TEST_CASE("EG13 FX starts with Granular on Part A only") {
  REQUIRE(paramDefault(partFxEngineParamIndex(0), PluginKind::Effect) == 1.0f);
  REQUIRE(paramDefault(partFxEngineParamIndex(1), PluginKind::Effect) == 0.0f);
  REQUIRE(paramDefault(partFxEngineParamIndex(0), PluginKind::Instrument) == 0.0f);
  REQUIRE(paramDefault(partFxEngineParamIndex(1), PluginKind::Instrument) == 0.0f);
  REQUIRE(text(partFxEngineParamIndex(0), 0) == "Off");
  REQUIRE(text(partFxEngineParamIndex(0), 1) == "Granular");
  REQUIRE(text(partFxEngineParamIndex(0), 2) == "Granular Delay");
  REQUIRE(text(partFxEngineParamIndex(0), 3) == "Resonator");
  REQUIRE(text(partFxEngineParamIndex(0), 4) == "Reverb");
  REQUIRE(text(partFxEngineParamIndex(0), 20) == "Off");  // no engine: Off
}

TEST_CASE("Part FX P names and text follow the selected engine") {
  char buf[64];
  PartEngines e;
  e.partFx = int(PartFxEngine::Granular);
  REQUIRE(std::string(paramName(partFxParamIndex(0, 4), e)) == "Density");
  formatParam(partFxParamIndex(0, 4), 0.75, buf, sizeof(buf), e);
  REQUIRE(std::string(buf) == "75%");
  e.partFx = int(PartFxEngine::Resonator);
  REQUIRE(std::string(paramName(partFxParamIndex(0, 4), e)) == "Spread");
  e.partFx = int(PartFxEngine::Reverb);
  REQUIRE(std::string(paramName(partFxParamIndex(1, 15), e)) == "(unused)");
  formatParam(partFxParamIndex(1, 15), 0.5, buf, sizeof(buf), e);
  REQUIRE(std::string(buf) == "-");
  e.partFx = int(PartFxEngine::Off);
  REQUIRE(std::string(paramName(partFxParamIndex(0, 0), e)) == "(unused)");
  REQUIRE(std::string(paramName(partFxEngineParamIndex(0), e)) == "Type");
  SlotKind kind = SlotKind::Source;
  int part = -1, p = -1;
  REQUIRE(slotParamOf(partFxParamIndex(1, 3), &kind, &part, &p));
  REQUIRE(kind == SlotKind::PartFx);
  REQUIRE(part == 1);
  REQUIRE(p == 3);
  REQUIRE(slotSelectorOf(partFxEngineParamIndex(1), &kind, &part));
  REQUIRE(kind == SlotKind::PartFx);
  REQUIRE(part == 1);
}
