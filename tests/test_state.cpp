#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

#include "engine/engine_registry.h"
#include "params/param_ids.h"
#include "state/state.h"
#include "synth/param_table.h"
#include "synth/synth.h"

using namespace eg13;

namespace {
const uint32_t kModel = params::id(0, params::Slot::Source, 1);
const uint32_t kTimbre = params::id(0, params::Slot::Source, 3);
uint32_t partId(int part, PartParam p) { return paramSpec(partParamIndex(part, p)).id; }
uint32_t masterId(MasterParam m) { return paramSpec(masterParamIndex(m)).id; }
}  // namespace

TEST_CASE("State round-trips every parameter exactly") {
  Synth a(PluginKind::Instrument);
  a.setParam(kModel, 0.9);
  a.setParam(kTimbre, 0.123456789);
  a.setParam(partId(1, PartParam::VoiceCount), 3);
  a.setParam(partId(0, PartParam::Pan), -0.4);
  a.setParam(masterId(MasterParam::Mode), 2);
  state::StateStore store;
  const std::string json = store.save(a);

  Synth b(PluginKind::Instrument);
  state::StateStore other;
  REQUIRE(other.load(b, json) == state::LoadResult::Ok);
  for (size_t i = 0; i < kParamCount; ++i) {
    REQUIRE(b.param(paramSpec(i).id) == a.param(paramSpec(i).id));
  }
}

TEST_CASE("Saved state has decimal parameter IDs") {
  Synth a(PluginKind::Instrument);
  const std::string json = state::StateStore{}.save(a);
  REQUIRE(json.find("\"schemaVersion\":3") != std::string::npos);
  REQUIRE(json.find("\"257\":") != std::string::npos);       // Part A Source P1
  REQUIRE(json.find("\"16711680\":") != std::string::npos);  // master level
}

TEST_CASE("Schema 1 documents from M0 load unchanged") {
  Synth s(PluginKind::Instrument);
  state::StateStore store;
  REQUIRE(store.load(s, R"({"schemaVersion":1,"params":{"257":0.9,"16711680":0.5}})") ==
          state::LoadResult::Ok);
  REQUIRE(s.param(kModel) == double(0.9f));
  REQUIRE(s.param(params::kMasterLevel) == 0.5);
  REQUIRE(store.save(s).find("\"schemaVersion\":3") != std::string::npos);
}

TEST_CASE("Unusable state is rejected and leaves the synth unchanged") {
  Synth s(PluginKind::Instrument);
  s.setParam(kTimbre, 0.25);
  state::StateStore store;
  for (const char* bad : {"", "not json", "[]", "{}", R"({"schemaVersion":2})",
                          R"({"schemaVersion":"2","params":{}})",
                          R"({"schemaVersion":0,"params":{}})",
                          R"({"schemaVersion":2,"params":[]})"}) {
    INFO(bad);
    REQUIRE(store.load(s, bad) == state::LoadResult::Invalid);
    REQUIRE(s.param(kTimbre) == 0.25);
  }
}

TEST_CASE("Missing parameters fall back to the defaults for the synth's kind") {
  Synth fx(PluginKind::Effect);
  fx.setParam(masterId(MasterParam::Dry), 0.2);
  state::StateStore store;
  REQUIRE(store.load(fx, R"({"schemaVersion":2,"params":{"257":0.9}})") == state::LoadResult::Ok);
  REQUIRE(fx.param(masterId(MasterParam::Dry)) ==
          double(paramDefault(masterParamIndex(MasterParam::Dry), PluginKind::Effect)));
  REQUIRE(fx.param(kTimbre) == double(paramDefault(sourceParamIndex(0, 2), PluginKind::Effect)));
}

TEST_CASE("Unknown data is preserved on resave") {
  Synth s(PluginKind::Instrument);
  state::StateStore store;
  const char* json =
      R"({"schemaVersion":2,"params":{"257":0.9,"999999":0.5},"future":{"x":1}})";
  REQUIRE(store.load(s, json) == state::LoadResult::OkWithWarnings);
  const std::string saved = store.save(s);
  REQUIRE(saved.find("\"999999\":0.5") != std::string::npos);
  REQUIRE(saved.find("\"future\":{\"x\":1}") != std::string::npos);

  REQUIRE(store.load(s, R"({"schemaVersion":2,"params":{}})") == state::LoadResult::Ok);
  const std::string resaved = store.save(s);
  REQUIRE(resaved.find("999999") == std::string::npos);
  REQUIRE(resaved.find("future") == std::string::npos);
}

TEST_CASE("Malformed entries are skipped with a warning") {
  Synth s(PluginKind::Instrument);
  state::StateStore store;
  const char* json = R"({"schemaVersion":2,"params":{"257":0.9,"abc":0.5,"259":"x"}})";
  REQUIRE(store.load(s, json) == state::LoadResult::OkWithWarnings);
  REQUIRE(s.param(kModel) == double(0.9f));
  REQUIRE(s.param(kTimbre) == double(paramDefault(sourceParamIndex(0, 2), PluginKind::Instrument)));
  REQUIRE(store.save(s).find("abc") == std::string::npos);
}

TEST_CASE("Newer schema versions load what is understood, with a warning") {
  Synth s(PluginKind::Instrument);
  state::StateStore store;
  REQUIRE(store.load(s, R"({"schemaVersion":7,"params":{"257":0.9}})") ==
          state::LoadResult::OkWithWarnings);
  REQUIRE(s.param(kModel) == double(0.9f));
}

TEST_CASE("Loaded values are clamped and stepped values rounded") {
  Synth s(PluginKind::Instrument);
  state::StateStore store;
  const std::string json = R"({"schemaVersion":2,"params":{"259":1.7,")" +
                           std::to_string(partId(0, PartParam::VoiceCount)) + R"(":5.6}})";
  REQUIRE(store.load(s, json) == state::LoadResult::Ok);
  REQUIRE(s.param(kTimbre) == 1.0);
  REQUIRE(s.param(partId(0, PartParam::VoiceCount)) == 6.0);
}

TEST_CASE("Huge loaded values clamp instead of overflowing") {
  Synth s(PluginKind::Instrument);
  state::StateStore store;
  REQUIRE(store.load(s, R"({"schemaVersion":2,"params":{"259":1e300}})") == state::LoadResult::Ok);
  REQUIRE(s.param(kTimbre) == 1.0);
}

TEST_CASE("A load while processing is reflected immediately") {
  Synth s(PluginKind::Instrument);
  s.prepare(48000.0, 256);
  s.setActive(true);
  state::StateStore store;
  REQUIRE(store.load(s, R"({"schemaVersion":2,"params":{"16711680":0.25}})") ==
          state::LoadResult::Ok);
  REQUIRE(s.param(params::kMasterLevel) == 0.25);
  s.setActive(false);
}

TEST_CASE("Saved state is schema 3 and carries every engine's settings") {
  Synth a(PluginKind::Instrument);
  a.setParam(paramSpec(sourceParamIndex(0, 1)).id, 0.9);  // Macro Harmonics
  a.setParam(paramSpec(sourceEngineParamIndex(0)).id, 1);  // switch to Digital (stashes Macro's P1..P8)
  const std::string json = state::StateStore{}.save(a);
  REQUIRE(json.find("\"schemaVersion\":3") != std::string::npos);
  REQUIRE(json.find("\"engineSettings\"") != std::string::npos);

  Synth b(PluginKind::Instrument);
  state::StateStore store;
  REQUIRE(store.load(b, json) == state::LoadResult::Ok);
  REQUIRE(b.engine(0) == 1);
  const EngineStash stash = b.engineStash();
  const EngineStash original = a.engineStash();
  for (size_t i = 0; i < kStashSize; ++i) REQUIRE(stash[i] == original[i]);
  REQUIRE(stash[stashIndex(0, 0, 1)] == 0.9f);
}

TEST_CASE("Schema 2 FX project keeps Macro Oscillator") {
  Synth fx(PluginKind::Effect);
  state::StateStore store;
  REQUIRE(store.load(fx, R"({"schemaVersion":2,"params":{"257":0.9,"16711681":1.0}})") ==
          state::LoadResult::Ok);
  REQUIRE(fx.engine(0) == int(SourceEngine::Macro));
  REQUIRE(fx.engine(1) == int(SourceEngine::Macro));
  REQUIRE(fx.param(257) == double(0.9f));                  // Macro Model
  REQUIRE(fx.param(params::id(params::kMasterPart, params::Slot::Part, 1)) == 1.0);  // Dry
}

TEST_CASE("Schema 3 without engineSettings uses each engine's defaults") {
  Synth s(PluginKind::Instrument);
  state::StateStore store;
  REQUIRE(store.load(s, R"({"schemaVersion":3,"params":{}})") == state::LoadResult::Ok);
  const EngineStash stash = s.engineStash();
  const EngineStash defaults = Synth::defaultStash();
  for (size_t i = 0; i < kStashSize; ++i) REQUIRE(stash[i] == defaults[i]);
}

TEST_CASE("Malformed engineSettings entries are dropped with a warning") {
  Synth s(PluginKind::Instrument);
  state::StateStore store;
  const char* json =
      R"({"schemaVersion":3,"params":{},"engineSettings":{"0":{"1":[0.1,0.2],"9":[1,1,1,1,1,1,1,1],)"
      R"("2":[0.5,0.5,0.5,0.5,0.5,0.5,0.5,"x"],"3":[0.3,0.3,0.3,0.3,0.3,0.3,0.3,0.3]},"7":{}}})";
  REQUIRE(store.load(s, json) == state::LoadResult::OkWithWarnings);
  const EngineStash stash = s.engineStash();
  REQUIRE(stash[stashIndex(0, 3, 0)] == 0.3f);
  REQUIRE(stash[stashIndex(0, 1, 0)] == Synth::defaultStash()[stashIndex(0, 1, 0)]);
  // Master parameters 7..9 also serialise as "7": value, so look for the part object.
  REQUIRE(store.save(s).find("\"7\":{") == std::string::npos);
}

namespace {
std::string engineArray(size_t count, double value) {
  std::string s = "[";
  for (size_t i = 0; i < count; ++i) s += (i ? "," : "") + std::to_string(value);
  return s + "]";
}
}  // namespace

TEST_CASE("State round-trips all 16 Source slots and every engine's stash") {
  Synth a(PluginKind::Instrument);
  for (int p = 0; p < int(kSourceParamCount); ++p) {
    a.setParam(paramSpec(sourceParamIndex(0, p)).id, 0.02 * (p + 1));
  }
  a.setParam(paramSpec(sourceEngineParamIndex(0)).id, 3);  // stash Macro's 16 values
  a.setParam(paramSpec(sourceParamIndex(0, 15)).id, 0.75);  // Resonator slot 16
  const std::string json = state::StateStore{}.save(a);

  Synth b(PluginKind::Instrument);
  state::StateStore store;
  REQUIRE(store.load(b, json) == state::LoadResult::Ok);
  REQUIRE(b.param(paramSpec(sourceParamIndex(0, 15)).id) == 0.75);
  const EngineStash stash = b.engineStash();
  for (int p = 0; p < int(kSourceParamCount); ++p) {
    INFO(p);
    REQUIRE(stash[stashIndex(0, 0, p)] == float(0.02 * (p + 1)));
  }
  const EngineStash original = a.engineStash();
  for (size_t i = 0; i < kStashSize; ++i) REQUIRE(stash[i] == original[i]);
}

TEST_CASE("engineSettings arrays have 16 numbers") {
  Synth a(PluginKind::Instrument);
  const std::string json = state::StateStore{}.save(a);
  REQUIRE(json.find("\"engineSettings\":{\"0\":{\"0\":[") != std::string::npos);
  const size_t start = json.find('[', json.find("\"engineSettings\""));
  const size_t end = json.find(']', start);
  const std::string list = json.substr(start, end - start);
  REQUIRE(size_t(std::count(list.begin(), list.end(), ',')) == kSourceParamCount - 1);
}

TEST_CASE("An 8-number engineSettings array loads as P1..P8") {
  Synth s(PluginKind::Instrument);
  state::StateStore store;
  const std::string json = R"({"schemaVersion":3,"params":{},"engineSettings":{"1":{"2":)" +
                           engineArray(8, 0.25) + "}}}";
  REQUIRE(store.load(s, json) == state::LoadResult::Ok);
  const EngineStash stash = s.engineStash();
  const EngineStash defaults = Synth::defaultStash();
  for (int p = 0; p < int(kSourceParamCount); ++p) {
    INFO(p);
    REQUIRE(stash[stashIndex(1, 2, p)] == (p < 8 ? 0.25f : defaults[stashIndex(1, 2, p)]));
  }
}

TEST_CASE("engineSettings arrays of other lengths are dropped with a warning") {
  for (size_t count : {size_t(9), size_t(15), size_t(17)}) {
    INFO(count);
    Synth s(PluginKind::Instrument);
    state::StateStore store;
    const std::string json = R"({"schemaVersion":3,"params":{},"engineSettings":{"1":{"2":)" +
                             engineArray(count, 0.25) + R"(,"3":)" + engineArray(16, 0.5) + "}}}";
    REQUIRE(store.load(s, json) == state::LoadResult::OkWithWarnings);
    const EngineStash stash = s.engineStash();
    const EngineStash defaults = Synth::defaultStash();
    for (int p = 0; p < int(kSourceParamCount); ++p) {
      REQUIRE(stash[stashIndex(1, 2, p)] == defaults[stashIndex(1, 2, p)]);
      REQUIRE(stash[stashIndex(1, 3, p)] == 0.5f);
    }
  }
}

TEST_CASE("Schema 2 documents load P9..P16 at their defaults") {
  Synth s(PluginKind::Instrument);
  s.setParam(paramSpec(sourceParamIndex(0, 12)).id, 0.6);
  state::StateStore store;
  REQUIRE(store.load(s, R"({"schemaVersion":2,"params":{"257":0.9}})") == state::LoadResult::Ok);
  for (int p = 8; p < int(kSourceParamCount); ++p) {
    REQUIRE(s.param(paramSpec(sourceParamIndex(0, p)).id) ==
            double(paramDefault(sourceParamIndex(0, p), PluginKind::Instrument)));
  }
}
