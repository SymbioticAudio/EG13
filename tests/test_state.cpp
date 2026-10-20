#include <catch2/catch_test_macros.hpp>

#include <string>

#include "params/param_ids.h"
#include "state/state.h"
#include "synth/param_table.h"
#include "synth/synth.h"

using namespace eg13;

namespace {
const uint32_t kModel = params::id(0, params::Slot::Source, 1);
const uint32_t kTimbre = params::id(0, params::Slot::Source, 3);
}  // namespace

TEST_CASE("State round-trips every parameter exactly") {
  Synth a(PluginKind::Instrument);
  a.setParam(kModel, 0.9);
  a.setParam(kTimbre, 0.123456789);
  a.setParam(params::kMasterLevel, 0.3);
  const std::string json = state::save(a);

  Synth b(PluginKind::Instrument);
  REQUIRE(state::load(b, json) == state::LoadResult::Ok);
  for (size_t i = 0; i < kParamCount; ++i) {
    REQUIRE(b.param(paramSpec(i).id) == a.param(paramSpec(i).id));
  }
}

TEST_CASE("Saved state has a schema version and decimal parameter IDs") {
  Synth a(PluginKind::Instrument);
  const std::string json = state::save(a);
  REQUIRE(json.find("\"schemaVersion\":1") != std::string::npos);
  REQUIRE(json.find("\"257\":") != std::string::npos);       // Part A Source P1
  REQUIRE(json.find("\"16711680\":") != std::string::npos);  // master level
}

TEST_CASE("Unusable state is rejected and leaves the synth unchanged") {
  Synth s(PluginKind::Instrument);
  s.setParam(kTimbre, 0.25);
  for (const char* bad : {"", "not json", "[]", "{}", R"({"schemaVersion":1})",
                          R"({"schemaVersion":"1","params":{}})",
                          R"({"schemaVersion":0,"params":{}})",
                          R"({"schemaVersion":1,"params":[]})"}) {
    INFO(bad);
    REQUIRE(state::load(s, bad) == state::LoadResult::Invalid);
    REQUIRE(s.param(kTimbre) == 0.25);
  }
}

TEST_CASE("Missing parameters fall back to defaults") {
  Synth s(PluginKind::Instrument);
  s.setParam(kTimbre, 0.25);
  REQUIRE(state::load(s, R"({"schemaVersion":1,"params":{"257":0.9}})") ==
          state::LoadResult::Ok);
  REQUIRE(s.param(kModel) == double(float(0.9)));
  REQUIRE(s.param(kTimbre) == double(paramSpec(2).defaultValue));
}

TEST_CASE("Unknown or malformed entries are skipped with a warning") {
  Synth s(PluginKind::Instrument);
  const char* json =
      R"({"schemaVersion":1,"params":{"257":0.9,"999999":0.5,"abc":0.5,"259":"x"}})";
  REQUIRE(state::load(s, json) == state::LoadResult::OkWithWarnings);
  REQUIRE(s.param(kModel) == double(float(0.9)));
  REQUIRE(s.param(kTimbre) == double(paramSpec(2).defaultValue));
}

TEST_CASE("Newer schema versions load what is understood, with a warning") {
  Synth s(PluginKind::Instrument);
  const char* json = R"({"schemaVersion":7,"params":{"257":0.9},"future":{"x":1}})";
  REQUIRE(state::load(s, json) == state::LoadResult::OkWithWarnings);
  REQUIRE(s.param(kModel) == double(float(0.9)));
}

TEST_CASE("Out-of-range values are clamped on load") {
  Synth s(PluginKind::Instrument);
  REQUIRE(state::load(s, R"({"schemaVersion":1,"params":{"259":1.7}})") ==
          state::LoadResult::Ok);
  REQUIRE(s.param(kTimbre) == 1.0);
}
