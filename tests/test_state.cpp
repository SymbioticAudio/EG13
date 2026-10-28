#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

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
// Whether stash entry i belongs to an engine the synth has selected: engineStash() reports the
// live P values there rather than the stored ones.
bool currentEntry(const Synth& s, size_t i) {
  for (int k = 0; k < kNumSlotKinds; ++k) {
    for (int part = 0; part < kNumParts; ++part) {
      const SlotKind kind = SlotKind(k);
      const size_t first = slotStashIndex(kind, part, s.engine(kind, part), 0);
      if (i >= first && i < first + slotParamCount(kind)) return true;
    }
  }
  return false;
}
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
  REQUIRE(json.find("\"schemaVersion\":6") != std::string::npos);
  REQUIRE(json.find("\"257\":") != std::string::npos);       // Part A Source P1
  REQUIRE(json.find("\"16711680\":") != std::string::npos);  // master level
}

TEST_CASE("Unusable state is rejected and leaves the synth unchanged") {
  Synth s(PluginKind::Instrument);
  s.setParam(kTimbre, 0.25);
  state::StateStore store;
  for (const char* bad : {"", "not json", "[]", "{}", R"({"schemaVersion":6})",
                          R"({"schemaVersion":"5","params":{}})",
                          R"({"schemaVersion":0,"params":{}})",
                          R"({"schemaVersion":6,"params":[]})"}) {
    INFO(bad);
    REQUIRE(store.load(s, bad) == state::LoadResult::Invalid);
    REQUIRE(s.param(kTimbre) == 0.25);
  }
}

TEST_CASE("Missing parameters fall back to the defaults for the synth's kind") {
  Synth fx(PluginKind::Effect);
  fx.setParam(masterId(MasterParam::Dry), 0.2);
  state::StateStore store;
  REQUIRE(store.load(fx, R"({"schemaVersion":6,"params":{"257":0.9}})") == state::LoadResult::Ok);
  REQUIRE(fx.param(masterId(MasterParam::Dry)) ==
          double(paramDefault(masterParamIndex(MasterParam::Dry), PluginKind::Effect)));
  REQUIRE(fx.param(kTimbre) == double(paramDefault(sourceParamIndex(0, 2), PluginKind::Effect)));
}

TEST_CASE("Unknown data is preserved on resave") {
  Synth s(PluginKind::Instrument);
  state::StateStore store;
  const char* json =
      R"({"schemaVersion":6,"params":{"257":0.9,"999999":0.5},"future":{"x":1}})";
  REQUIRE(store.load(s, json) == state::LoadResult::OkWithWarnings);
  const std::string saved = store.save(s);
  REQUIRE(saved.find("\"999999\":0.5") != std::string::npos);
  REQUIRE(saved.find("\"future\":{\"x\":1}") != std::string::npos);

  REQUIRE(store.load(s, R"({"schemaVersion":6,"params":{}})") == state::LoadResult::Ok);
  const std::string resaved = store.save(s);
  REQUIRE(resaved.find("\"999999\":") == std::string::npos);
  REQUIRE(resaved.find("future") == std::string::npos);
}

TEST_CASE("Malformed entries are skipped with a warning") {
  Synth s(PluginKind::Instrument);
  state::StateStore store;
  const char* json = R"({"schemaVersion":6,"params":{"257":0.9,"abc":0.5,"259":"x"}})";
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

TEST_CASE("Documents older than schema 6 load as the init state, with a warning") {
  for (PluginKind kind : {PluginKind::Instrument, PluginKind::Effect}) {
    for (const char* old : {R"({"schemaVersion":5,"params":{"257":0.9},"engineSettings":{"0":{"1":[0.1]}},"x":1})",
                            R"({"schemaVersion":1,"params":{"257":0.9,"16711680":0.5}})",
                            R"({"schemaVersion":5,"params":{"257":0.9},"partFxSettings":{"0":{"1":[0.1]}}})"}) {
      INFO(old);
      Synth s(kind);
      state::StateStore store;
      // Extras kept from an earlier load: an unknown parameter and key, and a newer version's route.
      REQUIRE(store.load(s, R"({"schemaVersion":6,"params":{"999999":0.5},"future":{"x":1},)"
                            R"("routes":{"0":[[1,2,0,4]],"2":[[5,5,5]]}})") == state::LoadResult::OkWithWarnings);
      s.setParam(kTimbre, 0.123);
      s.setRoute(1, 3, Route{kSrcModWheel, kDestPan, kSrcNone});
      REQUIRE(store.load(s, old) == state::LoadResult::OkWithWarnings);
      const Synth init(kind);
      for (size_t i = 0; i < kParamCount; ++i) REQUIRE(s.param(paramSpec(i).id) == init.param(paramSpec(i).id));
      REQUIRE(s.engineStash() == init.engineStash());
      REQUIRE(s.patch().routes == init.patch().routes);
      REQUIRE(s.patch().shapes == init.patch().shapes);
      // The next save is this version's own: nothing of either document is written back.
      REQUIRE(nlohmann::json::parse(store.save(s)) == nlohmann::json::parse(state::StateStore{}.save(init)));
    }
  }
}

TEST_CASE("Loaded values are clamped and stepped values rounded") {
  Synth s(PluginKind::Instrument);
  state::StateStore store;
  const std::string json = R"({"schemaVersion":6,"params":{"259":1.7,")" +
                           std::to_string(partId(0, PartParam::VoiceCount)) + R"(":5.6}})";
  REQUIRE(store.load(s, json) == state::LoadResult::Ok);
  REQUIRE(s.param(kTimbre) == 1.0);
  REQUIRE(s.param(partId(0, PartParam::VoiceCount)) == 6.0);
}

TEST_CASE("Huge loaded values clamp instead of overflowing") {
  Synth s(PluginKind::Instrument);
  state::StateStore store;
  REQUIRE(store.load(s, R"({"schemaVersion":6,"params":{"259":1e300}})") == state::LoadResult::Ok);
  REQUIRE(s.param(kTimbre) == 1.0);
}

TEST_CASE("A load while processing is reflected immediately") {
  Synth s(PluginKind::Instrument);
  s.prepare(48000.0, 256);
  s.setActive(true);
  state::StateStore store;
  REQUIRE(store.load(s, R"({"schemaVersion":6,"params":{"16711680":0.25}})") ==
          state::LoadResult::Ok);
  REQUIRE(s.param(params::kMasterLevel) == 0.25);
  s.setActive(false);
}

TEST_CASE("Saved state is schema 6 and carries every engine's settings") {
  Synth a(PluginKind::Instrument);
  a.setParam(paramSpec(sourceParamIndex(0, 1)).id, 0.9);  // Macro Harmonics
  a.setParam(paramSpec(sourceEngineParamIndex(0)).id, 1);  // switch to Digital (stashes Macro's P1..P8)
  const std::string json = state::StateStore{}.save(a);
  REQUIRE(json.find("\"schemaVersion\":6") != std::string::npos);
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

TEST_CASE("Malformed engineSettings entries are dropped with a warning") {
  Synth s(PluginKind::Instrument);
  state::StateStore store;
  const char* json =
      R"({"schemaVersion":6,"params":{},"engineSettings":{"0":{"1":[0.1,0.2],"9":[1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1],)"
      R"("2":[0.5,0.5,0.5,0.5,0.5,0.5,0.5,0.5,0.5,0.5,0.5,0.5,0.5,0.5,0.5,"x"],)"
      R"("3":[0.3,0.3,0.3,0.3,0.3,0.3,0.3,0.3,0.3,0.3,0.3,0.3,0.3,0.3,0.3,0.3]},"7":{}}})";
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

TEST_CASE("engineSettings arrays of other lengths are dropped with a warning") {
  for (size_t count : {size_t(8), size_t(9), size_t(15), size_t(17)}) {
    INFO(count);
    Synth s(PluginKind::Instrument);
    state::StateStore store;
    const std::string json = R"({"schemaVersion":6,"params":{},"engineSettings":{"1":{"2":)" +
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

namespace {
uint32_t filterTypeId(int part) { return paramSpec(filterEngineParamIndex(part)).id; }
uint32_t shaperTypeId(int part) { return paramSpec(shaperEngineParamIndex(part)).id; }
}  // namespace

TEST_CASE("Filter and Shaper settings round-trip with every engine's stash") {
  Synth a(PluginKind::Instrument);
  a.setParam(filterTypeId(0), double(FilterEngine::Analog));
  a.setParam(paramSpec(filterParamIndex(0, 0)).id, 0.3);
  a.setParam(filterTypeId(0), double(FilterEngine::StateVariable));  // stashes the Analog's 0.3
  a.setParam(shaperTypeId(1), double(ShaperEngine::Eq));
  a.setParam(paramSpec(shaperParamIndex(1, 1)).id, 0.8);
  a.setParam(shaperTypeId(1), double(ShaperEngine::RingMod));
  a.setParam(paramSpec(shaperParamIndex(1, 8)).id, 0.75);  // Warps Mode: frequency shifter
  const std::string json = state::StateStore{}.save(a);
  REQUIRE(json.find("\"filterSettings\"") != std::string::npos);
  REQUIRE(json.find("\"shaperSettings\"") != std::string::npos);

  Synth b(PluginKind::Instrument);
  state::StateStore store;
  REQUIRE(store.load(b, json) == state::LoadResult::Ok);
  REQUIRE(b.engine(SlotKind::Filter, 0) == int(FilterEngine::StateVariable));
  REQUIRE(b.engine(SlotKind::Shaper, 1) == int(ShaperEngine::RingMod));
  REQUIRE(b.param(paramSpec(shaperParamIndex(1, 8)).id) == 0.75);
  const EngineStash stash = b.engineStash();
  const EngineStash original = a.engineStash();
  for (size_t i = 0; i < kStashSize; ++i) REQUIRE(stash[i] == original[i]);
  REQUIRE(stash[slotStashIndex(SlotKind::Filter, 0, int(FilterEngine::Analog), 0)] == 0.3f);
  REQUIRE(stash[slotStashIndex(SlotKind::Shaper, 1, int(ShaperEngine::Eq), 1)] == 0.8f);
}

TEST_CASE("filterSettings and shaperSettings arrays have 4 and 12 numbers") {
  const std::string json = state::StateStore{}.save(Synth(PluginKind::Instrument));
  auto firstListLength = [&](const char* key) {
    const size_t start = json.find('[', json.find(key));
    const size_t end = json.find(']', start);
    const std::string list = json.substr(start, end - start);
    return size_t(std::count(list.begin(), list.end(), ',')) + 1;
  };
  REQUIRE(firstListLength("\"filterSettings\"") == kFilterParamCount);
  REQUIRE(firstListLength("\"shaperSettings\"") == kShaperParamCount);
}

TEST_CASE("Malformed filterSettings and shaperSettings entries are dropped with a warning") {
  Synth s(PluginKind::Instrument);
  state::StateStore store;
  const char* json =
      R"({"schemaVersion":6,"params":{},)"
      R"("filterSettings":{"0":{"1":[0.1,0.2,0.3],"2":[0.4,0.4,0.4,0.4],"9":[1,1,1,1]}},)"
      R"("shaperSettings":{"1":{"2":[0.5,0.5,0.5,0.5,0.5,0.5,0.5,0.5,0.5,0.5,0.5,0.5,0.5,0.5,0.5,0.5]}}})";
  REQUIRE(store.load(s, json) == state::LoadResult::OkWithWarnings);
  const EngineStash stash = s.engineStash();
  const EngineStash defaults = Synth::defaultStash();
  REQUIRE(stash[slotStashIndex(SlotKind::Filter, 0, 2, 0)] == 0.4f);  // the valid entry
  REQUIRE(stash[slotStashIndex(SlotKind::Filter, 0, 1, 0)] == defaults[slotStashIndex(SlotKind::Filter, 0, 1, 0)]);
  REQUIRE(stash[slotStashIndex(SlotKind::Shaper, 1, 2, 0)] == defaults[slotStashIndex(SlotKind::Shaper, 1, 2, 0)]);
  // Nothing else changed: the dropped entries (wrong length, engine 9) left no trace.
  for (size_t i = 0; i < kStashSize; ++i) {
    const bool valid = i >= slotStashIndex(SlotKind::Filter, 0, 2, 0) && i <= slotStashIndex(SlotKind::Filter, 0, 2, 3);
    if (!valid && !currentEntry(s, i)) REQUIRE(stash[i] == defaults[i]);
  }
}

TEST_CASE("Filter and Shaper settings: wrong types, out-of-range values and Off") {
  const EngineStash defaults = Synth::defaultStash();
  state::StateStore store;
  SECTION("a non-number element drops the entry") {
    Synth s(PluginKind::Instrument);
    REQUIRE(store.load(s, R"({"schemaVersion":6,"params":{},"filterSettings":{"0":{"1":[0.1,"x",0.3,0.4]}}})") ==
            state::LoadResult::OkWithWarnings);
    REQUIRE(s.engineStash()[slotStashIndex(SlotKind::Filter, 0, 1, 0)] == defaults[slotStashIndex(SlotKind::Filter, 0, 1, 0)]);
  }
  SECTION("values outside 0..1 clamp") {
    Synth s(PluginKind::Instrument);
    REQUIRE(store.load(s, R"({"schemaVersion":6,"params":{},"filterSettings":{"1":{"2":[-0.5,1.5,0.25,7]}}})") ==
            state::LoadResult::Ok);
    const EngineStash stash = s.engineStash();
    REQUIRE(stash[slotStashIndex(SlotKind::Filter, 1, 2, 0)] == 0.0f);
    REQUIRE(stash[slotStashIndex(SlotKind::Filter, 1, 2, 1)] == 1.0f);
    REQUIRE(stash[slotStashIndex(SlotKind::Filter, 1, 2, 2)] == 0.25f);
    REQUIRE(stash[slotStashIndex(SlotKind::Filter, 1, 2, 3)] == 1.0f);
  }
  SECTION("a part that is not an object, or a section that is not an object, is dropped") {
    Synth s(PluginKind::Instrument);
    REQUIRE(store.load(s, R"({"schemaVersion":6,"params":{},"filterSettings":{"0":[0.1,0.2,0.3,0.4]},"shaperSettings":7})") ==
            state::LoadResult::OkWithWarnings);
    const EngineStash stash = s.engineStash();
    for (size_t i = 0; i < kStashSize; ++i) {
      if (!currentEntry(s, i)) REQUIRE(stash[i] == defaults[i]);
    }
  }
  SECTION("Off's entry (engine 0) loads like any other") {
    Synth s(PluginKind::Instrument);
    const std::string eq = std::to_string(paramSpec(shaperEngineParamIndex(1)).id);
    const std::string json = R"({"schemaVersion":6,"params":{")" + eq + R"(":2},)" +
                             R"("shaperSettings":{"1":{"0":[0.1,0.2,0.3,0.4,0.5,0.6,0.7,0.8,0.9,1,0,0.5]}}})";
    REQUIRE(store.load(s, json) == state::LoadResult::Ok);
    REQUIRE(s.engine(SlotKind::Shaper, 1) == int(ShaperEngine::Eq));
    REQUIRE(s.engineStash()[slotStashIndex(SlotKind::Shaper, 1, 0, 4)] == 0.5f);
  }
}

TEST_CASE("Every slot, part and engine's settings round-trip to their own place") {
  // A distinct value for every stash entry, so a part, engine or P mixed up on the way shows.
  auto unique = [](size_t i) { return float(std::fmod(double(i + 1) * 0.6180339887498949, 1.0)); };
  Synth a(PluginKind::Instrument);
  for (int k = 0; k < kNumSlotKinds; ++k) {
    const SlotKind kind = SlotKind(k);
    for (int part = 0; part < kNumParts; ++part) {
      for (int e = slotEngineCount(kind) - 1; e >= 0; --e) {  // ends on engine 0
        a.setParam(paramSpec(slotEngineParamIndex(kind, part)).id, double(e));
        for (int p = 0; p < int(slotParamCount(kind)); ++p) {
          a.setParam(paramSpec(slotParamIndex(kind, part, p)).id, double(unique(slotStashIndex(kind, part, e, p))));
        }
      }
    }
  }
  const std::string json = state::StateStore{}.save(a);
  Synth b(PluginKind::Instrument);
  state::StateStore store;
  REQUIRE(store.load(b, json) == state::LoadResult::Ok);
  const EngineStash stash = b.engineStash();
  for (int k = 0; k < kNumSlotKinds; ++k) {
    const SlotKind kind = SlotKind(k);
    for (int part = 0; part < kNumParts; ++part) {
      REQUIRE(b.engine(kind, part) == 0);
      for (int e = 0; e < slotEngineCount(kind); ++e) {
        for (int p = 0; p < int(slotParamCount(kind)); ++p) {
          const size_t i = slotStashIndex(kind, part, e, p);
          REQUIRE(stash[i] == unique(i));
        }
      }
    }
  }
}

TEST_CASE("Part FX type, P's, input and per-engine settings round-trip") {
  Synth a(PluginKind::Instrument);
  const int types[kNumParts] = {1, 4};  // Granular on part 1, Reverb on part 2
  const double inputs[kNumParts] = {2, 1};
  for (int part = 0; part < kNumParts; ++part) {
    // Fill engine 3 (Resonator) first, then move on: its values are stashed.
    a.setParam(paramSpec(slotEngineParamIndex(SlotKind::PartFx, part)).id, 3.0);
    for (int p = 0; p < int(slotParamCount(SlotKind::PartFx)); ++p) {
      a.setParam(paramSpec(slotParamIndex(SlotKind::PartFx, part, p)).id, 0.05 + 0.05 * p + 0.01 * part);
    }
    a.setParam(paramSpec(slotEngineParamIndex(SlotKind::PartFx, part)).id, double(types[part]));
    for (int p = 0; p < int(slotParamCount(SlotKind::PartFx)); ++p) {
      a.setParam(paramSpec(slotParamIndex(SlotKind::PartFx, part, p)).id, 0.9 - 0.04 * p - 0.01 * part);
    }
    a.setParam(partId(part, PartParam::PartFxInput), inputs[part]);
  }
  const std::string json = state::StateStore{}.save(a);
  REQUIRE(json.find("\"partFxSettings\"") != std::string::npos);

  Synth b(PluginKind::Instrument);
  state::StateStore store;
  REQUIRE(store.load(b, json) == state::LoadResult::Ok);
  for (int part = 0; part < kNumParts; ++part) {
    REQUIRE(b.engine(SlotKind::PartFx, part) == types[part]);
    REQUIRE(b.param(partId(part, PartParam::PartFxInput)) == inputs[part]);
    for (int p = 0; p < int(slotParamCount(SlotKind::PartFx)); ++p) {
      const uint32_t id = paramSpec(slotParamIndex(SlotKind::PartFx, part, p)).id;
      REQUIRE(b.param(id) == a.param(id));
    }
    // Switch back to the stashed engine: its values return.
    b.setParam(paramSpec(slotEngineParamIndex(SlotKind::PartFx, part)).id, 3.0);
    for (int p = 0; p < int(slotParamCount(SlotKind::PartFx)); ++p) {
      const uint32_t id = paramSpec(slotParamIndex(SlotKind::PartFx, part, p)).id;
      REQUIRE(b.param(id) == double(float(0.05 + 0.05 * p + 0.01 * part)));
    }
  }
}

TEST_CASE("Mod slot settings and scopes round-trip per engine") {
  Synth a(PluginKind::Instrument);
  const uint32_t engineId = paramSpec(modEngineParamIndex(1, 2)).id;
  a.setParam(engineId, double(ModEngineKind::FunctionGenerator));
  a.setParam(paramSpec(modParamIndex(1, 2, 4)).id, 0.33);
  a.setParam(paramSpec(modScopeParamIndex(1, 2)).id, 1.0);
  a.setParam(engineId, double(ModEngineKind::Random));  // stashes the Function Generator's settings
  a.setParam(paramSpec(modScopeParamIndex(1, 2)).id, 0.0);
  const std::string json = state::StateStore{}.save(a);
  Synth b(PluginKind::Instrument);
  state::StateStore store;
  REQUIRE(store.load(b, json) == state::LoadResult::Ok);
  REQUIRE(b.engine(SlotKind::Mod3, 1) == int(ModEngineKind::Random));
  REQUIRE(b.param(paramSpec(modScopeParamIndex(1, 2)).id) == 0.0);
  const EngineStash stash = b.engineStash();
  REQUIRE(stash[slotStashIndex(SlotKind::Mod3, 1, int(ModEngineKind::FunctionGenerator), 4)] == 0.33f);
  REQUIRE(stash[scopeStashIndex(1, 2, int(ModEngineKind::FunctionGenerator))] == 1.0f);
  REQUIRE(stash[scopeStashIndex(1, 2, int(ModEngineKind::Random))] == 0.0f);
  b.setParam(engineId, double(ModEngineKind::FunctionGenerator));
  REQUIRE(b.param(paramSpec(modParamIndex(1, 2, 4)).id) == double(0.33f));
  REQUIRE(b.param(paramSpec(modScopeParamIndex(1, 2)).id) == 1.0);
}

TEST_CASE("mod settings arrays have 17 numbers; a scope is 0 or 1") {
  Synth s(PluginKind::Instrument);
  state::StateStore store;
  REQUIRE(store.load(s, R"({"schemaVersion":6,"params":{},"mod2Settings":{"0":{"4":[0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.7]}}})") ==
          state::LoadResult::Ok);
  const EngineStash stash = s.engineStash();
  REQUIRE(stash[slotStashIndex(SlotKind::Mod2, 0, 4, 15)] == 0.1f);
  REQUIRE(stash[scopeStashIndex(0, 1, 4)] == 1.0f);  // 0.7 rounds to per part
  REQUIRE(store.load(s, R"({"schemaVersion":6,"params":{},"mod2Settings":{"0":{"4":[0.1,0.1]}}})") ==
          state::LoadResult::OkWithWarnings);
}

TEST_CASE("Saved state is schema 6 and carries routes and envelope shapes") {
  Synth a(PluginKind::Instrument);
  a.setRoute(1, 7, Route{kSrcModWheel, uint16_t(kDestShaperP + 2), kSrcVelocity});
  EnvelopeShape shape;
  shape.count = 5;
  shape.type[4] = EnvelopeShape::Step;
  shape.loop[1] = 1;
  a.setEnvelopeShape(0, 3, shape);
  const std::string json = state::StateStore{}.save(a);
  Synth b(PluginKind::Instrument);
  state::StateStore store;
  REQUIRE(store.load(b, json) == state::LoadResult::Ok);
  const PatchData patch = b.patch();
  REQUIRE(patch.routes[1][7] == Route{kSrcModWheel, uint16_t(kDestShaperP + 2), kSrcVelocity});
  REQUIRE(patch.routes[0][0] == initPatchRoute(PluginKind::Instrument, 0, 0));  // the init patch's, saved
  REQUIRE(patch.shapes[0][3] == shape);
  REQUIRE(patch.shapes[1][3] == EnvelopeShape{});
  REQUIRE(b.appliedPatch().routes[1][7] == patch.routes[1][7]);  // inactive: applied at once
}

TEST_CASE("Malformed routes and envelope shapes are dropped with a warning") {
  Synth s(PluginKind::Instrument);
  state::StateStore store;
  const char* json =
      R"({"schemaVersion":6,"params":{},"routes":{"0":[[9,2,0],[1,"x",0],[1,2],[70000,1,0]],"5":[],"1":7},)"
      R"("envelopeShapes":{"0":[{"count":9,"types":[0,1,2,7,0,0],"loops":[0,1,0,0,0,3]},{"count":2},5]}})";
  REQUIRE(store.load(s, json) == state::LoadResult::OkWithWarnings);
  const PatchData patch = s.patch();
  REQUIRE(patch.routes[0][0] == Route{kSrcModWheel, kDestAmp, kSrcNone});
  REQUIRE(patch.routes[0][1] == Route{});
  REQUIRE(patch.routes[0][2] == Route{});
  REQUIRE(patch.routes[0][3] == Route{});
  REQUIRE(patch.routes[1][0] == Route{});
  REQUIRE(patch.shapes[0][0].count == 6);                      // clamped
  REQUIRE(patch.shapes[0][0].type[3] == EnvelopeShape::Ramp);  // unknown type
  REQUIRE(patch.shapes[0][0].loop[5] == 1);
  REQUIRE(patch.shapes[0][1] == EnvelopeShape{});  // malformed: the default
}

TEST_CASE("Routes and envelope shapes: wrong types, negative, float and non-object input") {
  struct Case {
    const char* json;
    state::LoadResult result;
  };
  for (const Case c : {
           Case{R"({"schemaVersion":6,"params":{},"routes":7})", state::LoadResult::OkWithWarnings},
           Case{R"({"schemaVersion":6,"params":{},"routes":[[1,2,0]]})", state::LoadResult::OkWithWarnings},
           Case{R"({"schemaVersion":6,"params":{},"routes":{"0":[[-1,2,0],[1.5,2,0],[1,2],"x",null,{}]}})",
                state::LoadResult::OkWithWarnings},
           Case{R"({"schemaVersion":6,"params":{},"routes":{"x":[[1,2,0]],"-1":[[1,2,0]],"99999999999":[]}})",
                state::LoadResult::OkWithWarnings},
           Case{R"({"schemaVersion":6,"params":{},"envelopeShapes":[1]})", state::LoadResult::OkWithWarnings},
           Case{R"({"schemaVersion":6,"params":{},"envelopeShapes":{"0":[{"count":2.5,"types":[0,0,0,0,0,0],)"
                R"("loops":[0,0,0,0,0,0]},{"count":2,"types":[0,0,0,0,0],"loops":[0,0,0,0,0,0]},)"
                R"({"count":2,"types":[0,0,"a",0,0,0],"loops":[0,0,0,0,0,0]},{}]}})",
                state::LoadResult::OkWithWarnings},
           Case{R"({"schemaVersion":6,"params":{},"envelopeShapes":{"1":[7,null,"x"],"0":[{"count":2}],"0x":[]}})",
                state::LoadResult::OkWithWarnings},
       }) {
    INFO(c.json);
    Synth s(PluginKind::Instrument);
    state::StateStore store;
    REQUIRE(store.load(s, c.json) == c.result);
    for (int part = 0; part < kNumParts; ++part) {
      for (int r = 0; r < kNumRoutes; ++r) REQUIRE(s.patch().routes[size_t(part)][size_t(r)] == Route{});
      for (int slot = 0; slot < kNumModSlots; ++slot) {
        REQUIRE(s.patch().shapes[size_t(part)][size_t(slot)] == EnvelopeShape{});
      }
    }
  }
  // Negative and out-of-range numbers in a well-formed shape are sanitized, not dropped.
  Synth s(PluginKind::Instrument);
  state::StateStore store;
  REQUIRE(store.load(s, R"({"schemaVersion":6,"params":{},"envelopeShapes":{"1":[{"count":-3,)"
                        R"("types":[-1,1,2,300,0,0],"loops":[0,0,-5,0,0,0]}]}})") == state::LoadResult::Ok);
  const EnvelopeShape shape = s.patch().shapes[1][0];
  REQUIRE(shape.count == 1);
  REQUIRE(shape.type[0] == EnvelopeShape::Ramp);
  REQUIRE(shape.type[1] == EnvelopeShape::Step);
  REQUIRE(shape.type[2] == EnvelopeShape::Hold);
  REQUIRE(shape.type[3] == EnvelopeShape::Ramp);
  REQUIRE(shape.loop[2] == 0);
}

TEST_CASE("Routes with codes from a newer version come back unchanged on resave") {
  Synth s(PluginKind::Instrument);
  state::StateStore store;
  // Route 1 uses a reserved source (17..31) into the Amp at 100 %: kept, and ignored (G4).
  const std::string amount = std::to_string(paramSpec(routeAmountParamIndex(0, 1)).id);
  REQUIRE(store.load(s, R"({"schemaVersion":6,"params":{")" + amount +
                            R"(":1.0},"routes":{"0":[[30,200,17],[17,2,0]]}})") == state::LoadResult::Ok);
  REQUIRE(s.patch().routes[0][0] == Route{30, 200, 17});
  REQUIRE(s.patch().routes[0][1] == Route{17, kDestAmp, kSrcNone});
  REQUIRE_FALSE(routeActive(s.patch().routes[0][0]));  // ignored by this version
  REQUIRE_FALSE(routeActive(s.patch().routes[0][1]));
  Synth again(PluginKind::Instrument);
  REQUIRE(store.load(again, store.save(s)) == state::LoadResult::Ok);
  REQUIRE(again.patch().routes[0][0] == Route{30, 200, 17});
  REQUIRE(again.patch().routes[0][1] == Route{17, kDestAmp, kSrcNone});
  // The loaded routes and amounts, summed as the voice sums them: the Amp is not routed.
  RouteSet set;
  for (int r = 0; r < kNumRoutes; ++r) {
    set.route[r] = again.appliedPatch().routes[0][size_t(r)];
    set.amount[r] = float(again.param(paramSpec(routeAmountParamIndex(0, r)).id));
  }
  REQUIRE(set.amount[1] == 1.0f);
  float source[kNumSources];
  for (float& v : source) v = 1.0f;
  RouteSums sums;
  sumRoutes(set, source, DestinationKind::Voice, &sums);
  REQUIRE_FALSE(sums.touched[kDestAmp]);
  REQUIRE(sums.value[kDestAmp] == 0.0f);
}

TEST_CASE("A part named twice in routes or envelopeShapes is read once, with a warning") {
  Synth s(PluginKind::Instrument);
  state::StateStore store;
  REQUIRE(store.load(s, R"({"schemaVersion":6,"params":{},"routes":{"0":[[9,2,0]],"00":[[4,3,0]]},)"
                        R"("envelopeShapes":{"0":[{"count":2,"types":[0,0,0,0,0,0],"loops":[0,0,0,0,0,0]}],)"
                        R"("00":[{"count":3,"types":[0,0,0,0,0,0],"loops":[0,0,0,0,0,0]}]}})") ==
          state::LoadResult::OkWithWarnings);
  REQUIRE(s.patch().routes[0][0] == Route{kSrcModWheel, kDestAmp, kSrcNone});  // "0" comes first
  REQUIRE(s.patch().shapes[0][0].count == 2);
  // Each alone loads clean.
  REQUIRE(store.load(s, R"({"schemaVersion":6,"params":{},"routes":{"00":[[4,3,0]]}})") == state::LoadResult::Ok);
  REQUIRE(s.patch().routes[0][0] == Route{kSrcGate, kDestPan, kSrcNone});
}

TEST_CASE("A newer version's extra routes, route numbers, segments and parts come back on resave") {
  // Schema 6: Part A has 13 routes (the last at [3,1,0]) and route 1 carries a fourth number;
  // a part "2"; Part B's shape 1 has 7 segments and there is a 5th shape.
  std::string routes = R"({"0":[[9,2,0,77])";
  for (int r = 1; r < 12; ++r) routes += R"(,[1,4,0])";
  routes += R"(,[3,1,0]],"2":[[5,5,5]]})";
  const std::string shapes =
      R"({"1":[{"count":7,"types":[1,0,0,0,0,0,2],"loops":[0,0,0,0,0,0,1],"curve":3},)"
      R"({"count":2,"types":[0,0,0,0,0,0],"loops":[0,0,0,0,0,0]},)"
      R"({"count":2,"types":[0,0,0,0,0,0],"loops":[0,0,0,0,0,0]},)"
      R"({"count":2,"types":[0,0,0,0,0,0],"loops":[0,0,0,0,0,0]},{"count":9}],"7":[]})";
  const std::string json =
      R"({"schemaVersion":6,"params":{},"routes":)" + routes + R"(,"envelopeShapes":)" + shapes + "}";
  Synth s(PluginKind::Instrument);
  state::StateStore store;
  REQUIRE(store.load(s, json) == state::LoadResult::OkWithWarnings);  // newer schema
  REQUIRE(s.patch().routes[0][0] == Route{kSrcModWheel, kDestAmp, kSrcNone});  // its first three numbers
  REQUIRE(s.patch().routes[0][11] == Route{kSrcVelocity, kDestPartLevel, kSrcNone});
  REQUIRE(s.patch().shapes[1][0].count == 6);
  REQUIRE(s.patch().shapes[1][0].type[0] == EnvelopeShape::Step);
  REQUIRE(s.patch().shapes[1][3].count == 2);

  const nlohmann::json saved = nlohmann::json::parse(store.save(s));
  const nlohmann::json original = nlohmann::json::parse(json);
  REQUIRE(saved["routes"]["0"].size() == 13);
  REQUIRE(saved["routes"]["0"] == original["routes"]["0"]);  // the fourth number and the 13th route
  REQUIRE(saved["routes"]["2"] == original["routes"]["2"]);
  REQUIRE(saved["routes"]["1"].size() == 12);  // this version's own Part B
  REQUIRE(saved["envelopeShapes"]["1"] == original["envelopeShapes"]["1"]);
  REQUIRE(saved["envelopeShapes"]["7"] == original["envelopeShapes"]["7"]);

  // An edit replaces the entry it changes; the rest stays as the newer version saved it.
  s.setRoute(0, 0, Route{kSrcGate, kDestPan, kSrcNone});
  EnvelopeShape shape;
  shape.count = 3;
  s.setEnvelopeShape(1, 0, shape);
  const nlohmann::json edited = nlohmann::json::parse(store.save(s));
  REQUIRE(edited["routes"]["0"][0] == nlohmann::json::array({kSrcGate, kDestPan, kSrcNone}));
  REQUIRE(edited["routes"]["0"][12] == original["routes"]["0"][12]);
  REQUIRE(edited["envelopeShapes"]["1"][0]["count"] == 3);
  REQUIRE(edited["envelopeShapes"]["1"][0].find("curve") == edited["envelopeShapes"]["1"][0].end());
  REQUIRE(edited["envelopeShapes"]["1"][4] == original["envelopeShapes"]["1"][4]);

  // The resave is schema 5 with the newer data in it: it loads back the same (with a warning:
  // more than schema 5 holds) and keeps that data again.
  Synth again(PluginKind::Instrument);
  state::StateStore store2;
  REQUIRE(store2.load(again, edited.dump()) == state::LoadResult::OkWithWarnings);
  REQUIRE(again.patch().routes == s.patch().routes);
  REQUIRE(again.patch().shapes == s.patch().shapes);
  REQUIRE(nlohmann::json::parse(store2.save(again)) == edited);

  // A load without that data forgets it: the next save is this version's own.
  Synth fresh(PluginKind::Instrument);
  REQUIRE(store.load(fresh, R"({"schemaVersion":6,"params":{}})") == state::LoadResult::Ok);
  const nlohmann::json own = nlohmann::json::parse(store.save(fresh));
  REQUIRE(own["routes"].size() == size_t(kNumParts));
  REQUIRE(own["routes"]["0"].size() == size_t(kNumRoutes));
  REQUIRE(own["envelopeShapes"]["1"].size() == size_t(kNumModSlots));
}

TEST_CASE("A schema 6 document holding more than schema 6 does warns, reads what fits and keeps the rest") {
  std::string routes = R"({"0":[[1,2,0])";
  for (int r = 1; r < 13; ++r) routes += R"(,[4,3,0])";
  routes += "]}";
  for (const std::string& json :
       {R"({"schemaVersion":6,"params":{},"routes":)" + routes + "}",
        std::string(R"({"schemaVersion":6,"params":{},"routes":{"0":[[1,2,0,4]]}})"),
        std::string(R"({"schemaVersion":6,"params":{},"routes":{"2":[[1,2,0]]}})"),
        std::string(R"({"schemaVersion":6,"params":{},"envelopeShapes":{"0":[{"count":2,"types":[0,0,0,0,0,0,0],)"
                    R"("loops":[0,0,0,0,0,0,0]}]}})"),
        std::string(R"({"schemaVersion":6,"params":{},"envelopeShapes":{"0":[{"count":2,"types":[0,0,0,0,0,0],)"
                    R"("loops":[0,0,0,0,0,0],"curve":1}]}})")}) {
    INFO(json);
    Synth s(PluginKind::Instrument);
    state::StateStore store;
    REQUIRE(store.load(s, json) == state::LoadResult::OkWithWarnings);
    const nlohmann::json original = nlohmann::json::parse(json);
    const nlohmann::json saved = nlohmann::json::parse(store.save(s));
    for (const char* key : {"routes", "envelopeShapes"}) {
      if (original.find(key) == original.end()) continue;
      for (auto it = original[key].begin(); it != original[key].end(); ++it) {  // each entry as loaded
        for (size_t i = 0; i < it.value().size(); ++i) REQUIRE(saved[key][it.key()][i] == it.value()[i]);
      }
    }
  }
  Synth s(PluginKind::Instrument);
  state::StateStore store;
  REQUIRE(store.load(s, R"({"schemaVersion":6,"params":{},"routes":)" + routes + "}") ==
          state::LoadResult::OkWithWarnings);
  REQUIRE(s.patch().routes[0][0] == Route{kSrcVelocity, kDestAmp, kSrcNone});
  REQUIRE(s.patch().routes[0][11] == Route{kSrcGate, kDestPan, kSrcNone});
}

TEST_CASE("Browser state round-trips through StateStore, and a pre-M7 state gives an empty browser string") {
  Synth synth(PluginKind::Instrument);
  state::StateStore store;
  REQUIRE(store.browser().empty());
  const std::string saved0 = nlohmann::json::parse(store.save(synth)).dump();
  REQUIRE_FALSE(nlohmann::json::parse(saved0).contains("browser"));  // nothing to say: no key

  const std::string browser = R"({"name":"Glass \"Pad\"","edited":true})";
  store.setBrowser(browser);
  const std::string saved = store.save(synth);
  REQUIRE(nlohmann::json::parse(saved)["browser"] == browser);  // stored as an opaque string

  state::StateStore other;
  REQUIRE(other.load(synth, saved) == state::LoadResult::Ok);  // a known key: no warning
  REQUIRE(other.browser() == browser);
  REQUIRE(nlohmann::json::parse(other.save(synth))["browser"] == browser);

  // A state without the key (saved before M7, or by a store that had none) clears it.
  REQUIRE(other.load(synth, saved0) == state::LoadResult::Ok);
  REQUIRE(other.browser().empty());
  // A browser value that is not a string is ignored.
  auto odd = nlohmann::json::parse(saved0);
  odd["browser"] = 5;
  REQUIRE(other.load(synth, odd.dump()) == state::LoadResult::Ok);
  REQUIRE(other.browser().empty());
}

TEST_CASE("Saving on one thread while another sets the browser string never tears it") {
  Synth synth(PluginKind::Instrument);
  state::StateStore store;
  const std::string a(4096, 'a');
  const std::string b(8192, 'b');
  std::atomic<bool> stop{false};
  std::thread writer([&] {
    for (int i = 0; !stop; ++i) store.setBrowser(i % 2 ? a : b);
  });
  bool torn = false;
  for (int i = 0; i < 200; ++i) {
    const auto root = nlohmann::json::parse(store.save(synth));
    if (!root.contains("browser")) continue;  // saved before the first set
    const std::string value = root["browser"];
    if (value != a && value != b) torn = true;
  }
  stop = true;
  writer.join();
  REQUIRE_FALSE(torn);
}
