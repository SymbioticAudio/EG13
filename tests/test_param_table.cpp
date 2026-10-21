#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <set>
#include <string>

#include "engine/audio_input_engine.h"
#include "engine/engine_registry.h"
#include "engine/ripples_engine.h"
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

TEST_CASE("The table has 104 parameters with unique IDs") {
  REQUIRE(kSourceParamCount == 16);
  REQUIRE(kFilterParamCount == 4);
  REQUIRE(kShaperParamCount == 12);
  REQUIRE(kParamsPerPart == 50);
  REQUIRE(kParamCount == 104);
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
  // Layout per part: Source selector + P1..P16, Filter + P1..P4, Shaper + P1..P12, part settings.
  REQUIRE(filterEngineParamIndex(0) == 17);
  REQUIRE(shaperEngineParamIndex(0) == 22);
  REQUIRE(partParamIndex(0, PartParam::Level) == 35);
  REQUIRE(sourceEngineParamIndex(1) == 50);
  REQUIRE(masterParamIndex(MasterParam::Level) == 100);
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
    REQUIRE(paramDefault(filterEngineParamIndex(part), PluginKind::Instrument) == 0.0f);
    REQUIRE(paramDefault(shaperEngineParamIndex(part), PluginKind::Instrument) == 0.0f);
    REQUIRE(paramDefault(shaperEngineParamIndex(part), PluginKind::Effect) == 0.0f);
  }
  REQUIRE(paramDefault(filterEngineParamIndex(0), PluginKind::Effect) == float(FilterEngine::Analog));
  REQUIRE(paramDefault(filterEngineParamIndex(1), PluginKind::Effect) == 0.0f);
  REQUIRE(paramDefault(filterParamIndex(0, 0), PluginKind::Effect) ==
          RipplesEngine::staticInfo().params[0].defaultValue);
  // Filter/Shaper P host defaults are one value per position, whatever the default engine
  // (final review I1): Off ignores them, the other engines get a sensible reset.
  REQUIRE(paramDefault(filterParamIndex(0, 0), PluginKind::Instrument) == 1.0f);
  REQUIRE(paramDefault(filterParamIndex(1, 1), PluginKind::Instrument) == 0.0f);
  REQUIRE(paramDefault(shaperParamIndex(0, 1), PluginKind::Instrument) == 0.5f);  // EQ gain 0 dB
  REQUIRE(paramDefault(shaperParamIndex(1, 3), PluginKind::Effect) == 0.5f);
  REQUIRE(std::string(paramSpec(filterParamIndex(0, 0)).name) == "(unused)");
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
  for (int e = 0; e < slotEngineCount(SlotKind::Filter); ++e) {
    REQUIRE(slotEngineInfo(SlotKind::Filter, e).paramCount <= int(kFilterParamCount));
  }
  for (int e = 0; e < slotEngineCount(SlotKind::Shaper); ++e) {
    REQUIRE(slotEngineInfo(SlotKind::Shaper, e).paramCount <= int(kShaperParamCount));
  }
  REQUIRE(slotEngineFromSelector(SlotKind::Shaper, 2.0) == 2);
  REQUIRE(slotEngineFromSelector(SlotKind::Filter, 9.0) == 0);
}

