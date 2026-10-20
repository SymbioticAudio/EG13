#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <set>
#include <string>

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

TEST_CASE("The table has 48 parameters with unique IDs") {
  REQUIRE(kParamCount == 48);
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

TEST_CASE("Dry defaults differ between the instrument and the FX") {
  const size_t dry = masterParamIndex(MasterParam::Dry);
  REQUIRE(paramDefault(dry, PluginKind::Instrument) == 0.0f);
  REQUIRE(paramDefault(dry, PluginKind::Effect) == 1.0f);
  const size_t voices = partParamIndex(0, PartParam::VoiceCount);
  REQUIRE(paramDefault(voices, PluginKind::Instrument) == 8.0f);
  REQUIRE(paramDefault(voices, PluginKind::Effect) == 8.0f);
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
  REQUIRE(text(masterParamIndex(MasterParam::Mode), 2) == "Split");
  REQUIRE(text(masterParamIndex(MasterParam::Dry), 0.25) == "25%");
  REQUIRE(text(sourceParamIndex(1, 0), (8.0 + 0.5) / 24.0) == "Virtual analog");
}

TEST_CASE("glideSeconds maps 0..1 to 0..2 s quadratically") {
  REQUIRE(glideSeconds(0.0f) == 0.0f);
  REQUIRE(glideSeconds(0.5f) == 0.5f);
  REQUIRE(glideSeconds(1.0f) == 2.0f);
}
