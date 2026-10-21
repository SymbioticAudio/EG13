#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

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
  REQUIRE(json.find("\"schemaVersion\":4") != std::string::npos);
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
  REQUIRE(store.save(s).find("\"schemaVersion\":4") != std::string::npos);
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
  REQUIRE(resaved.find("\"999999\":") == std::string::npos);
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

TEST_CASE("Saved state is schema 4 and carries every engine's settings") {
  Synth a(PluginKind::Instrument);
  a.setParam(paramSpec(sourceParamIndex(0, 1)).id, 0.9);  // Macro Harmonics
  a.setParam(paramSpec(sourceEngineParamIndex(0)).id, 1);  // switch to Digital (stashes Macro's P1..P8)
  const std::string json = state::StateStore{}.save(a);
  REQUIRE(json.find("\"schemaVersion\":4") != std::string::npos);
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
  // The engines in use report their live values (a host default may differ from the engine's own,
  // e.g. the init patch's cutoff); every other engine holds its own defaults.
  for (size_t i = 0; i < kSlotStashSize; ++i) {
    if (!currentEntry(s, i)) REQUIRE(stash[i] == defaults[i]);
  }
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

namespace {
uint32_t filterTypeId(int part) { return paramSpec(filterEngineParamIndex(part)).id; }
uint32_t shaperTypeId(int part) { return paramSpec(shaperEngineParamIndex(part)).id; }
}  // namespace

TEST_CASE("Schema 3 projects load with Filter and Shaper Off and pass audio as before") {
  Synth fx(PluginKind::Effect);
  state::StateStore store;
  REQUIRE(store.load(fx, R"({"schemaVersion":3,"params":{"16711681":1.0}})") == state::LoadResult::Ok);
  for (int part = 0; part < kNumParts; ++part) {
    REQUIRE(fx.engine(SlotKind::Filter, part) == int(FilterEngine::Off));
    REQUIRE(fx.engine(SlotKind::Shaper, part) == int(ShaperEngine::Off));
    for (int p = 0; p < int(kFilterParamCount); ++p) REQUIRE(fx.param(paramSpec(filterParamIndex(part, p)).id) == 0.0);
  }
  REQUIRE(fx.engine(SlotKind::Source, 0) == int(SourceEngine::AudioInput));
  // M2's EG13 FX passed the input at unity, aligned with Dry: still so.
  fx.prepare(44100.0, 256);
  const size_t latency = size_t(fx.latencySamples());
  std::vector<float> l(13230), r(13230), ol(13230), orr(13230);
  for (size_t i = 0; i < l.size(); ++i) {
    l[i] = 0.5f * float(std::sin(6.283185307179586 * 220.0 * double(i) / 44100.0));
    r[i] = -l[i];
  }
  for (size_t done = 0; done < l.size(); done += 256) {
    const int n = int(std::min<size_t>(256, l.size() - done));
    const float* in[2] = {l.data() + done, r.data() + done};
    float* out[2] = {ol.data() + done, orr.data() + done};
    fx.process(in, out, nullptr, n);
  }
  for (size_t i = latency + 2205; i < l.size(); ++i) {
    REQUIRE(std::fabs(ol[i] - 2.0f * l[i - latency]) < 1e-4f);  // wet + Dry (Dry 1.0)
  }
}

TEST_CASE("A new EG13 FX starts with the Analog Filter, a schema 4 load keeps what it says") {
  Synth fx(PluginKind::Effect);
  REQUIRE(fx.engine(SlotKind::Filter, 0) == int(FilterEngine::Analog));
  state::StateStore store;
  const std::string json = state::StateStore{}.save(fx);
  Synth again(PluginKind::Effect);
  again.setParam(filterTypeId(0), 0);
  REQUIRE(store.load(again, json) == state::LoadResult::Ok);
  REQUIRE(again.engine(SlotKind::Filter, 0) == int(FilterEngine::Analog));
}

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
      R"({"schemaVersion":4,"params":{},)"
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
    REQUIRE(store.load(s, R"({"schemaVersion":4,"params":{},"filterSettings":{"0":{"1":[0.1,"x",0.3,0.4]}}})") ==
            state::LoadResult::OkWithWarnings);
    REQUIRE(s.engineStash()[slotStashIndex(SlotKind::Filter, 0, 1, 0)] == defaults[slotStashIndex(SlotKind::Filter, 0, 1, 0)]);
  }
  SECTION("values outside 0..1 clamp") {
    Synth s(PluginKind::Instrument);
    REQUIRE(store.load(s, R"({"schemaVersion":4,"params":{},"filterSettings":{"1":{"2":[-0.5,1.5,0.25,7]}}})") ==
            state::LoadResult::Ok);
    const EngineStash stash = s.engineStash();
    REQUIRE(stash[slotStashIndex(SlotKind::Filter, 1, 2, 0)] == 0.0f);
    REQUIRE(stash[slotStashIndex(SlotKind::Filter, 1, 2, 1)] == 1.0f);
    REQUIRE(stash[slotStashIndex(SlotKind::Filter, 1, 2, 2)] == 0.25f);
    REQUIRE(stash[slotStashIndex(SlotKind::Filter, 1, 2, 3)] == 1.0f);
  }
  SECTION("a part that is not an object, or a section that is not an object, is dropped") {
    Synth s(PluginKind::Instrument);
    REQUIRE(store.load(s, R"({"schemaVersion":4,"params":{},"filterSettings":{"0":[0.1,0.2,0.3,0.4]},"shaperSettings":7})") ==
            state::LoadResult::OkWithWarnings);
    const EngineStash stash = s.engineStash();
    for (size_t i = 0; i < kStashSize; ++i) {
      if (!currentEntry(s, i)) REQUIRE(stash[i] == defaults[i]);
    }
  }
  SECTION("Off's entry (engine 0) loads like any other") {
    Synth s(PluginKind::Instrument);
    const std::string eq = std::to_string(paramSpec(shaperEngineParamIndex(1)).id);
    const std::string json = R"({"schemaVersion":4,"params":{")" + eq + R"(":2},)" +
                             R"("shaperSettings":{"1":{"0":[0.1,0.2,0.3,0.4,0.5,0.6,0.7,0.8,0.9,1,0,0.5]}}})";
    REQUIRE(store.load(s, json) == state::LoadResult::Ok);
    REQUIRE(s.engine(SlotKind::Shaper, 1) == int(ShaperEngine::Eq));
    REQUIRE(s.engineStash()[slotStashIndex(SlotKind::Shaper, 1, 0, 4)] == 0.5f);
  }
}

TEST_CASE("Older projects of both plugins load with Filter and Shaper Off and their P's at 0") {
  struct Case {
    PluginKind kind;
    int schema;
  };
  for (const Case c : {Case{PluginKind::Instrument, 1}, Case{PluginKind::Instrument, 3}, Case{PluginKind::Effect, 1},
                       Case{PluginKind::Effect, 2}, Case{PluginKind::Effect, 3}}) {
    INFO("schema " << c.schema << (c.kind == PluginKind::Effect ? " EG13 FX" : " EG13"));
    Synth s(c.kind);
    state::StateStore store;
    const std::string json = R"({"schemaVersion":)" + std::to_string(c.schema) + R"(,"params":{}})";
    REQUIRE(store.load(s, json) == state::LoadResult::Ok);
    for (int part = 0; part < kNumParts; ++part) {
      REQUIRE(s.engine(SlotKind::Filter, part) == int(FilterEngine::Off));
      REQUIRE(s.engine(SlotKind::Shaper, part) == int(ShaperEngine::Off));
      for (int p = 0; p < int(kFilterParamCount); ++p) REQUIRE(s.param(paramSpec(filterParamIndex(part, p)).id) == 0.0);
      for (int p = 0; p < int(kShaperParamCount); ++p) REQUIRE(s.param(paramSpec(shaperParamIndex(part, p)).id) == 0.0);
    }
  }
}

TEST_CASE("A schema 4 EG13 FX project saved with Filter Off stays Off") {
  Synth fx(PluginKind::Effect);
  fx.setParam(paramSpec(filterEngineParamIndex(0)).id, double(FilterEngine::Off));
  const std::string json = state::StateStore{}.save(fx);
  Synth again(PluginKind::Effect);
  state::StateStore store;
  REQUIRE(store.load(again, json) == state::LoadResult::Ok);
  REQUIRE(again.engine(SlotKind::Filter, 0) == int(FilterEngine::Off));
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
  REQUIRE(store.load(s, R"({"schemaVersion":4,"params":{},"mod2Settings":{"0":{"4":[0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.7]}}})") ==
          state::LoadResult::Ok);
  const EngineStash stash = s.engineStash();
  REQUIRE(stash[slotStashIndex(SlotKind::Mod2, 0, 4, 15)] == 0.1f);
  REQUIRE(stash[scopeStashIndex(0, 1, 4)] == 1.0f);  // 0.7 rounds to per part
  REQUIRE(store.load(s, R"({"schemaVersion":4,"params":{},"mod2Settings":{"0":{"4":[0.1,0.1]}}})") ==
          state::LoadResult::OkWithWarnings);
}
