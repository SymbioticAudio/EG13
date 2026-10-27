#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>
#include <nlohmann/json.hpp>

#include "core_api.h"
#include "alloc_counter.h"
#include "preset/factory.h"
#include "preset/preset.h"
#include "state/state.h"
#include "mod/mod_routes.h"
#include "synth/ui_snapshot.h"
#include "params/param_ids.h"
#include "synth/param_table.h"
#include "synth/synth.h"
#include "ui_bridge.h"

extern "C" uint32_t eg13_test_c_walk(const EG13CoreAPI* api);  // tests/core_api_c_check.c

using eg13::CoreApiContext;
using eg13::SlotKind;

namespace {

// One synth, its bridge and the function table: the plugin without CLAP. The editor edits through
// the table; drain() stands in for the audio thread's next process call.
struct Fixture {
  explicit Fixture(eg13::PluginKind kind = eg13::PluginKind::Instrument)
      : synth(kind), bridge(synth, {}), ctx{synth, bridge, {}, {}, {}, {}, {}, {}}, api(eg13::makeCoreApi(ctx)) {
    eg13::attachFactoryPresets(api);
    // The plugin's stateLoad path without the host: the store loads, the synth follows.
    ctx.loadState = [this](const std::string& json) { return int(store.load(synth, json)); };
    ctx.saveState = [this] { return store.save(synth); };
    ctx.getBrowser = [this] { return store.browser(); };
    ctx.setBrowser = [this](const std::string& value) { store.setBrowser(value); };
    ctx.markDirty = [this] { ++dirty; };
  }
  std::string name(uint32_t index) {
    char buf[EG13_TEXT_SIZE];
    REQUIRE(api.param_name(api.context, index, buf, sizeof buf));
    return buf;
  }
  void setEngine(eg13::params::Slot slot, double selector) {
    const uint32_t id = eg13::params::id(0, slot, 0);
    api.begin_edit(api.context, id);
    api.edit(api.context, id, selector);
    api.end_edit(api.context, id);
    bridge.drain(nullptr);
  }
  std::string srcName(uint32_t part, uint16_t code) {
    char buf[EG13_TEXT_SIZE];
    return api.source_name(api.context, part, code, buf, sizeof buf) ? buf : "<none>";
  }
  std::string destName(uint32_t part, uint16_t code) {
    char buf[EG13_TEXT_SIZE];
    return api.destination_name(api.context, part, code, buf, sizeof buf) ? buf : "<none>";
  }
  std::string applyMessage(const std::string& json, int target, int32_t* result = nullptr) {
    char message[256] = "";
    const int32_t r = api.preset_apply(api.context, json.c_str(), target, message, sizeof message);
    if (result) *result = r;
    return message;
  }
  std::string save(int part, const char* name = "Mine") {
    const uint32_t need = api.preset_save(api.context, part, name, nullptr, 0);
    std::string out(need, '\0');
    REQUIRE(api.preset_save(api.context, part, name, out.data(), need) == need);
    out.resize(need - 1);
    return out;
  }
  eg13::Synth synth;
  eg13::state::StateStore store;
  int dirty = 0;
  eg13::UiBridge bridge;
  CoreApiContext ctx;
  EG13CoreAPI api;
};

}  // namespace

TEST_CASE("param_name follows the engine (Part FX P5: Density with Granular, Spread with Resonator)") {
  Fixture f;
  const uint32_t p5 = uint32_t(eg13::partFxParamIndex(0, 4));
  f.setEngine(eg13::params::Slot::PartFx, 1.0);  // Granular
  REQUIRE(f.api.current_engine(f.api.context, EG13_SLOT_KIND_PART_FX, 0) == 1);
  REQUIRE(f.name(p5) == "Density");
  f.setEngine(eg13::params::Slot::PartFx, 3.0);  // Resonator
  REQUIRE(f.api.current_engine(f.api.context, EG13_SLOT_KIND_PART_FX, 0) == 3);
  REQUIRE(f.name(p5) == "Spread");
  // Part 1 is its own engine.
  REQUIRE(f.api.current_engine(f.api.context, EG13_SLOT_KIND_PART_FX, 1) == 0);
}

TEST_CASE("engine_param_count matches EngineInfo for every slot and engine") {
  Fixture f;
  REQUIRE(f.api.param_count(f.api.context) == eg13::kParamCount);
  for (int kind = 0; kind < eg13::kNumSlotKinds; ++kind) {
    const uint32_t count = f.api.engine_count(f.api.context, uint32_t(kind));
    REQUIRE(count == uint32_t(eg13::slotEngineCount(SlotKind(kind))));
    for (uint32_t e = 0; e < count; ++e) {
      const eg13::EngineInfo& info = eg13::slotEngineInfo(SlotKind(kind), int(e));
      REQUIRE(f.api.engine_param_count(f.api.context, uint32_t(kind), e) == uint32_t(info.paramCount));
      char buf[EG13_TEXT_SIZE];
      REQUIRE(f.api.engine_name(f.api.context, uint32_t(kind), e, buf, sizeof buf));
      REQUIRE(std::string(buf) == info.displayName);
    }
    // Out of range reads nothing.
    char buf[EG13_TEXT_SIZE] = "x";
    REQUIRE_FALSE(f.api.engine_name(f.api.context, uint32_t(kind), count, buf, sizeof buf));
    REQUIRE(f.api.engine_param_count(f.api.context, uint32_t(kind), count) == 0);
  }
  REQUIRE(f.api.engine_count(f.api.context, 99) == 0);
}

TEST_CASE("param_info, param_index, param_text and bounds") {
  Fixture f;
  for (uint32_t i = 0; i < eg13::kParamCount; ++i) {
    EG13ParamInfo info{};
    REQUIRE(f.api.param_info(f.api.context, i, &info));
    const eg13::ParamSpec& spec = eg13::paramSpec(i);
    REQUIRE(info.id == spec.id);
    REQUIRE(info.min == double(spec.minValue));
    REQUIRE(info.max == double(spec.maxValue));
    REQUIRE(info.def == double(spec.defaultValue));
    REQUIRE(((info.flags & EG13_PARAM_STEPPED) != 0) == spec.stepped);
    REQUIRE(f.api.param_index(f.api.context, info.id) == int32_t(i));
  }
  REQUIRE(f.api.param_index(f.api.context, 0xFFFFFFFFu) == -1);
  EG13ParamInfo info{};
  REQUIRE_FALSE(f.api.param_info(f.api.context, uint32_t(eg13::kParamCount), &info));
  char buf[EG13_TEXT_SIZE];
  REQUIRE_FALSE(f.api.param_name(f.api.context, uint32_t(eg13::kParamCount), buf, sizeof buf));
  REQUIRE_FALSE(f.api.param_text(f.api.context, uint32_t(eg13::kParamCount), 0.0, buf, sizeof buf));

  const uint32_t level = uint32_t(eg13::partParamIndex(0, eg13::PartParam::Level));
  char expected[EG13_TEXT_SIZE];
  f.synth.formatParam(level, 0.5, expected, sizeof expected);
  REQUIRE(f.api.param_text(f.api.context, level, 0.5, buf, sizeof buf));
  REQUIRE(std::string(buf) == expected);

  char tiny[4];  // truncates, stays terminated
  REQUIRE(f.api.param_name(f.api.context, level, tiny, sizeof tiny));
  REQUIRE(std::strlen(tiny) <= 3);
}

TEST_CASE("edit through the API reaches the synth after a drain") {
  Fixture f;
  const uint32_t index = uint32_t(eg13::partParamIndex(1, eg13::PartParam::Level));
  const uint32_t id = eg13::paramSpec(index).id;
  const double target = 0.25;
  REQUIRE(f.synth.param(id) != target);
  f.api.begin_edit(f.api.context, id);
  f.api.edit(f.api.context, id, target);
  f.api.end_edit(f.api.context, id);
  f.bridge.drain(nullptr);
  REQUIRE(f.synth.param(id) == target);

  EG13Snapshot snap{};
  f.api.snapshot(f.api.context, &snap);
  REQUIRE(snap.param_count == eg13::kParamCount);
  REQUIRE(snap.values[index] == target);
}

TEST_CASE("snapshot reports engine changes and request_resize reaches the callback") {
  Fixture f;
  EG13Snapshot before{}, after{};
  f.api.snapshot(f.api.context, &before);
  f.setEngine(eg13::params::Slot::PartFx, 1.0);
  f.api.snapshot(f.api.context, &after);
  REQUIRE(after.engine_generation != before.engine_generation);

  f.api.request_resize(f.api.context, 1, 2);  // no callback: ignored
  uint32_t w = 0, h = 0;
  f.ctx.requestResize = [&](uint32_t width, uint32_t height) { w = width, h = height; };
  f.api.request_resize(f.api.context, 900, 570);
  REQUIRE(w == 900);
  REQUIRE(h == 570);
}

TEST_CASE("Two bridges are independent") {
  Fixture a, b;
  const uint32_t p5 = uint32_t(eg13::partFxParamIndex(0, 4));
  const uint32_t id = eg13::paramSpec(uint32_t(eg13::partParamIndex(0, eg13::PartParam::Level))).id;
  const double bBefore = b.synth.param(id);
  a.setEngine(eg13::params::Slot::PartFx, 1.0);
  a.api.begin_edit(a.api.context, id);
  a.api.edit(a.api.context, id, 0.125);
  a.api.end_edit(a.api.context, id);
  a.bridge.drain(nullptr);
  REQUIRE(a.synth.param(id) == 0.125);
  REQUIRE(a.name(p5) == "Density");
  b.bridge.drain(nullptr);
  REQUIRE(b.synth.param(id) == bBefore);
  REQUIRE(b.api.current_engine(b.api.context, EG13_SLOT_KIND_PART_FX, 0) == 0);
  REQUIRE(b.name(p5) != "Density");
  REQUIRE(a.api.context != b.api.context);
}

TEST_CASE("The table works from a C translation unit") {
  Fixture f;
  REQUIRE(eg13_test_c_walk(&f.api) >= eg13::kParamCount);
}

TEST_CASE("route_set then route_get round-trips and marks dirty") {
  Fixture f;
  int dirty = 0;
  f.ctx.markDirty = [&] { ++dirty; };
  EG13Route r{};
  REQUIRE(f.api.route_get(f.api.context, 0, 0, &r));
  REQUIRE(r.source == eg13::kSrcMod);  // init patch: Mod 1 out 1 -> Amp via Velocity
  REQUIRE(r.destination == eg13::kDestAmp);
  REQUIRE(r.amount != 0.0);
  const double amount = r.amount;
  REQUIRE(f.api.route_set(f.api.context, 0, 0, eg13::kSrcModWheel, eg13::kDestFilterP, eg13::kSrcVelocity));
  REQUIRE(dirty == 1);
  EG13Route back{};
  REQUIRE(f.api.route_get(f.api.context, 0, 0, &back));
  REQUIRE(back.source == eg13::kSrcModWheel);
  REQUIRE(back.destination == eg13::kDestFilterP);
  REQUIRE(back.via == eg13::kSrcVelocity);
  REQUIRE(back.amount == amount);
  REQUIRE(f.synth.patch().routes[0][0].destination == eg13::kDestFilterP);
  // 0 clears.
  REQUIRE(f.api.route_set(f.api.context, 1, 11, 0, 0, 0));
  REQUIRE(dirty == 2);
  REQUIRE_FALSE(f.api.route_get(f.api.context, 2, 0, &back));
  REQUIRE_FALSE(f.api.route_get(f.api.context, 0, 12, &back));
}

TEST_CASE("route_set rejects bad codes") {
  Fixture f;
  int dirty = 0;
  f.ctx.markDirty = [&] { ++dirty; };
  const eg13::Route before = f.synth.patch().routes[0][1];
  const uint16_t okSrc = eg13::kSrcVelocity, okDst = eg13::kDestPitch;
  REQUIRE_FALSE(f.api.route_set(f.api.context, 2, 1, okSrc, okDst, 0));
  REQUIRE_FALSE(f.api.route_set(f.api.context, 0, 12, okSrc, okDst, 0));
  REQUIRE_FALSE(f.api.route_set(f.api.context, 0, 1, 20, okDst, 0));    // reserved source
  REQUIRE_FALSE(f.api.route_set(f.api.context, 0, 1, 64, okDst, 0));    // past the sources
  REQUIRE_FALSE(f.api.route_set(f.api.context, 0, 1, okSrc, 5, 0));     // reserved destination
  REQUIRE_FALSE(f.api.route_set(f.api.context, 0, 1, okSrc, 200, 0));
  REQUIRE_FALSE(f.api.route_set(f.api.context, 0, 1, okSrc, okDst, 17));  // reserved via
  REQUIRE_FALSE(f.api.route_set(f.api.context, 0, 1, okSrc, eg13::modParamDestination(3, 0), 0));  // Mod 4 is Off
  REQUIRE(dirty == 0);
  REQUIRE(f.synth.patch().routes[0][1] == before);
}

TEST_CASE("names follow the engine") {
  Fixture f;
  REQUIRE(f.srcName(0, 0) == "<none>");
  REQUIRE(f.srcName(0, eg13::kSrcVelocity) == "Velocity");
  REQUIRE(f.srcName(0, 17) == "<none>");
  REQUIRE(f.destName(0, 0) == "<none>");
  REQUIRE(f.destName(0, eg13::kDestPitch) == "Pitch");
  REQUIRE(f.destName(0, eg13::kDestPartLevel) == "Part Level");
  REQUIRE(f.destName(0, 5) == "<none>");
  // Mod slots: Mod 1 is an Envelope in the init patch; Mod 4 is Off.
  REQUIRE(f.srcName(0, eg13::modOutputSource(0, 0)) == "Mod 1 Envelope");
  REQUIRE(f.srcName(0, eg13::modOutputSource(0, 1)) == "<none>");
  REQUIRE(f.srcName(0, eg13::modOutputSource(3, 0)) == "<none>");
  // Engine change: Mod 3 becomes an LFO (4 outputs).
  f.setEngine(eg13::params::Slot::Mod3, double(int(eg13::ModEngineKind::Lfo)));
  REQUIRE(f.synth.engine(eg13::SlotKind::Mod3, 0) == int(eg13::ModEngineKind::Lfo));
  REQUIRE(f.srcName(0, eg13::modOutputSource(2, 1)) == "Mod 3 LFO Out 2");
  REQUIRE(f.srcName(0, eg13::modOutputSource(2, 4)) == "<none>");
  REQUIRE(f.destName(0, eg13::modParamDestination(2, 0)).rfind("Mod 3: ", 0) == 0);
  REQUIRE(f.destName(0, eg13::modParamDestination(2, 15)) == "<none>");
  // Part FX P5 follows its engine; the name is "Part FX: <engine's P name>".
  const uint16_t fx5 = uint16_t(eg13::kDestPartFxP + 4);
  f.setEngine(eg13::params::Slot::PartFx, 1.0);
  REQUIRE(f.destName(0, fx5) == "Part FX: Density");
  f.setEngine(eg13::params::Slot::PartFx, 3.0);
  REQUIRE(f.destName(0, fx5) == "Part FX: Spread");
  REQUIRE(f.destName(1, fx5) != "Part FX: Spread");  // part 1 has its own engine
  // A Filter P is "Filter: <name>"; P's past the engine's count are hidden.
  const std::string filter = f.destName(0, eg13::kDestFilterP);
  REQUIRE(filter.rfind("Filter: ", 0) == 0);
  const int count = eg13::slotEngineInfo(SlotKind::Filter, f.synth.engine(SlotKind::Filter, 0)).paramCount;
  REQUIRE(f.destName(0, uint16_t(eg13::kDestFilterP + count)) == "<none>");
}

TEST_CASE("destination_for_param maps Filter P1 to 32 and P2 to 33") {
  Fixture f;
  using eg13::params::Slot;
  auto dest = [&](uint32_t part, Slot slot, uint32_t index) {
    return f.api.destination_for_param(f.api.context, eg13::params::id(part, slot, index));
  };
  REQUIRE(dest(0, Slot::Filter, 1) == eg13::kDestFilterP);
  REQUIRE(dest(0, Slot::Filter, 2) == eg13::kDestFilterP + 1);  // 33
  REQUIRE(dest(1, Slot::Filter, 2) == 33);
  REQUIRE(dest(0, Slot::Source, 1) == eg13::kDestSourceP);
  REQUIRE(dest(0, Slot::PartFx, 5) == eg13::kDestPartFxP + 4);
  REQUIRE(dest(0, Slot::Mod2, 3) == eg13::modParamDestination(1, 2));
  REQUIRE(dest(0, Slot::Filter, 0) == 0);  // the selector
  REQUIRE(dest(0, Slot::Mod1, 17) == 0);   // the scope
  REQUIRE(dest(0, Slot::Matrix, 1) == 0);
  REQUIRE(dest(0, Slot::Part, 1) == eg13::kDestPartLevel);  // Part Level
  REQUIRE(dest(1, Slot::Part, 1) == 4);
  REQUIRE(dest(0, Slot::Part, 2) == 0);  // Pan is not a route destination
  REQUIRE(dest(0, Slot::Part, 0) == 0);
  REQUIRE(f.api.destination_for_param(f.api.context, eg13::params::kMasterLevel) == 0);
  REQUIRE(f.api.destination_for_param(f.api.context, 0xFFFFFFFFu) == 0);
}

TEST_CASE("snapshot carries mod values and mod output ranges") {
  Fixture f;
  EG13Snapshot snap{};
  f.api.snapshot(f.api.context, &snap);
  // Nothing has run: no mod values (NaN) or ranges (+inf / -inf) are invented; the snapshot is
  // passed through as the core made it.
  eg13::UiSnapshot core;
  f.synth.uiSnapshot(&core);
  for (int p = 0; p < EG13_NUM_PARTS; ++p) {
    REQUIRE(std::isnan(snap.mod_value[p][0]));
    for (int d = 0; d < EG13_NUM_DESTINATIONS; ++d)
      REQUIRE(std::isnan(snap.mod_value[p][d]) == std::isnan(core.modValue[p][d]));
    for (int s = 0; s < EG13_NUM_MOD_SLOTS; ++s)
      for (int o = 0; o < EG13_MAX_MOD_OUTPUTS; ++o) {
        REQUIRE(snap.mod_out_min[p][s][o] == std::numeric_limits<float>::infinity());
        REQUIRE(snap.mod_out_max[p][s][o] == -std::numeric_limits<float>::infinity());
      }
  }
  // Play a note and render: values arrive.
  f.synth.prepare(48000.0, 64);
  f.synth.setActive(true);
  f.synth.noteOn(0, 60, 0.8f);
  float l[64], r[64];
  float* out[2] = {l, r};
  for (int i = 0; i < 20; ++i) f.synth.process(nullptr, out, nullptr, 64);
  f.api.snapshot(f.api.context, &snap);
  REQUIRE(std::isfinite(snap.mod_value[0][eg13::kDestAmp]));
  REQUIRE(snap.mod_out_min[0][0][0] <= snap.mod_out_max[0][0][0]);
}

TEST_CASE("version reads project version and git hash") {
  Fixture f;
  char buf[EG13_TEXT_SIZE];
  REQUIRE(f.api.version(f.api.context, buf, sizeof buf));
  const std::string v = buf;
  REQUIRE(v.find('(') != std::string::npos);
  REQUIRE(v.find(')') == v.size() - 1);
  REQUIRE(std::isdigit(static_cast<unsigned char>(v[0])));
  REQUIRE_FALSE(f.api.version(f.api.context, nullptr, 0));
}

namespace {

const uint32_t kEngineA = eg13::params::id(0, eg13::params::Slot::Source, 0);
const uint32_t kEngineB = eg13::params::id(1, eg13::params::Slot::Source, 0);

// A synth's sound as a preset file: both parts' source engines set.
std::string presetJson(eg13::PluginKind kind, int part, double engineA, double engineB) {
  eg13::Synth synth(kind);
  synth.setParam(kEngineA, engineA);
  synth.setParam(kEngineB, engineB);
  eg13::preset::Meta meta;
  meta.name = "Test";
  return eg13::preset::save(synth, eg13::state::StateStore(), meta, part);
}

// Puts a factory table in for the test and restores the generated one on every exit.
struct FactoryTable {
  FactoryTable(const eg13::preset::FactoryPreset* table, size_t count) {
    eg13::preset::setFactoryPresetsForTesting(table, count);
  }
  ~FactoryTable() { eg13::preset::setFactoryPresetsForTesting(nullptr, 0); }
};

eg13::preset::FactoryPreset entry(const char* bank, const char* name, eg13::PluginKind plugin, int order,
                                  const std::string& json, bool firstLoad = false,
                                  eg13::preset::Kind kind = eg13::preset::Kind::Performance) {
  return {bank, name, "note", "credit", "licence", kind, plugin, firstLoad, order, json.c_str()};
}

}  // namespace

TEST_CASE("preset_apply loads a Performance and marks the project dirty") {
  Fixture f;
  const std::string json = presetJson(eg13::PluginKind::Instrument, -1, 3, 5);
  REQUIRE(f.synth.param(kEngineA) != 3);
  int32_t result = -1;
  const std::string message = f.applyMessage(json, -1, &result);
  REQUIRE(result == EG13_PRESET_OK);
  REQUIRE(message.empty());
  REQUIRE(f.synth.param(kEngineA) == 3);
  REQUIRE(f.synth.param(kEngineB) == 5);
  REQUIRE(f.dirty == 1);
}

TEST_CASE("preset_apply into B leaves A") {
  Fixture f;
  f.synth.setParam(kEngineA, 2);
  const std::string patch = presetJson(eg13::PluginKind::Instrument, 0, 6, 7);  // part A's sound, stored as part 0
  int32_t result = -1;
  f.applyMessage(patch, 1, &result);
  REQUIRE(result == EG13_PRESET_OK);
  REQUIRE(f.synth.param(kEngineA) == 2);
  REQUIRE(f.synth.param(kEngineB) == 6);
}

TEST_CASE("preset_apply refuses an effect preset in the instrument, with a message") {
  Fixture f;
  const std::string json = presetJson(eg13::PluginKind::Effect, -1, 3, 5);
  int32_t result = -1;
  const std::string message = f.applyMessage(json, -1, &result);
  REQUIRE(result == EG13_PRESET_INVALID);
  REQUIRE_FALSE(message.empty());
  REQUIRE(f.synth.param(kEngineA) != 3);  // nothing changed
  REQUIRE(f.dirty == 0);
  f.applyMessage("not json at all", -1, &result);  // a broken file is refused too
  REQUIRE(result == EG13_PRESET_INVALID);
  REQUIRE(f.api.preset_apply(f.api.context, nullptr, -1, nullptr, 0) == EG13_PRESET_INVALID);
}

TEST_CASE("preset_save returns the needed size when the buffer is small") {
  Fixture f;
  f.synth.setParam(kEngineA, 4);
  const uint32_t need = f.api.preset_save(f.api.context, -1, "Glass", nullptr, 0);
  REQUIRE(need > 100);
  std::vector<char> small(need - 1, 'x');
  REQUIRE(f.api.preset_save(f.api.context, -1, "Glass", small.data(), uint32_t(small.size())) == need);
  REQUIRE(small[0] == 'x');  // nothing written: a truncated file would be unusable
  const std::string file = f.save(-1, "Glass");
  REQUIRE(file.size() + 1 == need);
  const auto meta = eg13::preset::readMeta(file);
  REQUIRE(meta);
  REQUIRE(meta->name == "Glass");
  REQUIRE(meta->bank == "User");
  REQUIRE(meta->kind == eg13::preset::Kind::Performance);
  REQUIRE(f.api.preset_save(f.api.context, 5, "x", nullptr, 0) == 0);  // no such part
  const auto patch = eg13::preset::readMeta(f.save(1));
  REQUIRE(patch);
  REQUIRE(patch->kind == eg13::preset::Kind::Patch);
  // What was saved loads back.
  Fixture g;
  int32_t result = -1;
  g.applyMessage(file, -1, &result);
  REQUIRE(result == EG13_PRESET_OK);
  REQUIRE(g.synth.param(kEngineA) == 4);
}

TEST_CASE("browser state round-trips through StateStore, and a pre-M7 state gives an empty browser string") {
  Fixture f;
  char buffer[128];
  REQUIRE(f.api.browser_state_get(f.api.context, buffer, sizeof buffer));
  REQUIRE(std::string(buffer).empty());
  const std::string browser = R"({"loaded":"Glass","edited":true})";
  f.api.browser_state_set(f.api.context, browser.c_str());
  REQUIRE(f.dirty == 1);
  f.api.browser_state_set(f.api.context, browser.c_str());  // the same value: not a change
  REQUIRE(f.dirty == 1);
  REQUIRE(f.api.browser_state_get(f.api.context, buffer, sizeof buffer));
  REQUIRE(browser == buffer);
  REQUIRE_FALSE(f.api.browser_state_get(f.api.context, buffer, uint32_t(browser.size())));  // no room for the NUL
  // A project reopened: the same string. A preset applied meanwhile leaves it to the UI.
  const std::string saved = f.store.save(f.synth);
  Fixture g;
  REQUIRE(g.store.load(g.synth, saved) == eg13::state::LoadResult::Ok);
  REQUIRE(g.api.browser_state_get(g.api.context, buffer, sizeof buffer));
  REQUIRE(browser == buffer);
  f.applyMessage(presetJson(eg13::PluginKind::Instrument, -1, 3, 5), -1);
  REQUIRE(f.api.browser_state_get(f.api.context, buffer, sizeof buffer));
  REQUIRE(browser == buffer);
  // Saved before M7: no key, so empty (the header then shows Init).
  auto old = nlohmann::json::parse(saved);
  old.erase("browser");
  REQUIRE(g.store.load(g.synth, old.dump()) == eg13::state::LoadResult::Ok);
  REQUIRE(g.api.browser_state_get(g.api.context, buffer, sizeof buffer));
  REQUIRE(std::string(buffer).empty());
}

TEST_CASE("factory_preset_count lists only this kind") {
  const std::string perf = presetJson(eg13::PluginKind::Instrument, -1, 1, 1);
  const std::string fx = presetJson(eg13::PluginKind::Effect, -1, 1, 1);
  const eg13::preset::FactoryPreset table[] = {
      entry("FX", "Fx One", eg13::PluginKind::Effect, 1, fx),
      entry("Bank 2", "Late", eg13::PluginKind::Instrument, 1, perf),
      entry("Bank 1", "Second", eg13::PluginKind::Instrument, 2, perf, false, eg13::preset::Kind::Patch),
      entry("Bank 1", "First", eg13::PluginKind::Instrument, 1, perf),
      entry("FX", "Fx Two", eg13::PluginKind::Effect, 2, fx),
  };
  FactoryTable guard(table, 5);
  EG13FactoryPreset out;
  {
    Fixture f;
    REQUIRE(f.api.factory_preset_count(f.api.context) == 3);
    const char* expected[] = {"First", "Second", "Late"};
    for (uint32_t i = 0; i < 3; ++i) {
      REQUIRE(f.api.factory_preset(f.api.context, i, &out));
      REQUIRE(std::string(out.name) == expected[i]);
      REQUIRE(std::string(out.json) == perf);
      REQUIRE(std::string(out.note) == "note");
    }
    REQUIRE(f.api.factory_preset(f.api.context, 1, &out));
    REQUIRE(out.kind == 1);
    REQUIRE(std::string(out.bank) == "Bank 1");
    REQUIRE_FALSE(f.api.factory_preset(f.api.context, 3, &out));
  }
  Fixture fx_(eg13::PluginKind::Effect);
  REQUIRE(fx_.api.factory_preset_count(fx_.api.context) == 2);
  REQUIRE(fx_.api.factory_preset(fx_.api.context, 0, &out));
  REQUIRE(std::string(out.name) == "Fx One");
  REQUIRE(out.kind == 0);
}

TEST_CASE("a new instrument instance starts on the firstLoad preset, and a new FX instance starts on Init") {
  const std::string first = presetJson(eg13::PluginKind::Instrument, -1, 3, 5);
  const std::string other = presetJson(eg13::PluginKind::Instrument, -1, 6, 7);
  const std::string fx = presetJson(eg13::PluginKind::Effect, -1, 3, 5);
  const eg13::preset::FactoryPreset table[] = {
      entry("Bank 1", "Other", eg13::PluginKind::Instrument, 1, other),
      entry("Bank 1", "Opener", eg13::PluginKind::Instrument, 2, first, true),
      entry("FX", "Fx", eg13::PluginKind::Effect, 1, fx, true),  // never opens EG13 FX
  };
  FactoryTable guard(table, 3);
  const auto apply = [](Fixture& f) {
    return eg13::applyFirstLoadPreset(f.synth.kind(), f.ctx.saveState, f.ctx.loadState, f.ctx.setBrowser);
  };
  Fixture instrument;
  REQUIRE(apply(instrument));
  // The browser string names the preset, so the header shows it, not Init.
  const auto browser = nlohmann::json::parse(instrument.store.browser());
  REQUIRE(browser["version"] == 1);
  REQUIRE(browser["loaded"].size() == 1);
  REQUIRE(browser["loaded"][0]["part"] == -1);
  REQUIRE(browser["loaded"][0]["bank"] == "Bank 1");
  REQUIRE(browser["loaded"][0]["name"] == "Opener");
  REQUIRE(browser["loaded"][0]["kind"] == "performance");
  REQUIRE(browser["edited"] == false);
  REQUIRE(instrument.synth.param(kEngineA) == 3);
  REQUIRE(instrument.synth.param(kEngineB) == 5);
  Fixture effect(eg13::PluginKind::Effect);
  const double fxA = effect.synth.param(kEngineA);
  REQUIRE_FALSE(apply(effect));
  REQUIRE(effect.synth.param(kEngineA) == fxA);
  // An instrument with no firstLoad preset keeps Init.
  const eg13::preset::FactoryPreset noFirst[] = {entry("Bank 1", "Other", eg13::PluginKind::Instrument, 1, other)};
  FactoryTable guard2(noFirst, 1);
  Fixture plain;
  REQUIRE_FALSE(apply(plain));
  REQUIRE(plain.synth.param(kEngineA) == Fixture().synth.param(kEngineA));
}

TEST_CASE("applying presets while notes play allocates nothing on the audio thread") {
  constexpr int kBlock = 128;
  Fixture f;
  f.synth.prepare(48000.0, kBlock);
  f.synth.setActive(true);
  const std::string perfA = presetJson(eg13::PluginKind::Instrument, -1, 3, 5);
  const std::string perfB = presetJson(eg13::PluginKind::Instrument, -1, 6, 2);
  const std::string patch = presetJson(eg13::PluginKind::Instrument, 0, 4, 4);

  std::atomic<bool> noteOff{false}, stop{false};
  std::atomic<size_t> allocations{0};
  std::atomic<long> blocks{0};
  std::thread audio([&] {
    std::vector<float> l(kBlock), r(kBlock);
    float* out[2] = {l.data(), r.data()};
    f.synth.beginBlock();
    f.synth.noteOn(0, 60, 0.8f);
    f.synth.noteOn(0, 64, 0.8f);
    f.synth.noteOn(0, 67, 0.8f);
    bool released = false;
    eg13::test::ScopedAllocationCounter counter;
    while (!stop.load(std::memory_order_acquire)) {
      f.synth.beginBlock();
      if (!released && noteOff.load(std::memory_order_acquire)) {
        for (int key : {60, 64, 67}) f.synth.noteOff(0, key);
        released = true;
      }
      f.synth.process(nullptr, out, nullptr, kBlock);
      blocks.fetch_add(1, std::memory_order_relaxed);
    }
    allocations.store(counter.count());
  });
  for (int i = 0; i < 50; ++i) {
    int32_t result = -1;
    f.applyMessage(i % 3 == 0 ? perfA : i % 3 == 1 ? perfB : patch, i % 3 == 2 ? 1 : -1, &result);
    REQUIRE(result == EG13_PRESET_OK);
    const long seen = blocks.load();
    while (blocks.load() < seen + 4) std::this_thread::yield();  // let a few blocks run between loads
  }
  eg13::UiSnapshot snap;
  f.synth.uiSnapshot(&snap);
  REQUIRE(snap.activeVoices[0] + snap.activeVoices[1] > 0);  // the notes were still sounding
  noteOff.store(true, std::memory_order_release);
  // Release: the audio thread runs on until no voice sounds (or 50 s of waiting).
  for (int wait = 0; wait < 5000; ++wait) {
    f.synth.uiSnapshot(&snap);
    if (snap.activeVoices[0] + snap.activeVoices[1] == 0 && blocks.load() > 0) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  stop.store(true, std::memory_order_release);
  audio.join();
  f.synth.uiSnapshot(&snap);
  REQUIRE(allocations.load() == 0);
  REQUIRE(snap.activeVoices[0] + snap.activeVoices[1] == 0);  // nothing stuck
  f.synth.setActive(false);
}

namespace {

// Dirties everything an Init must reset: params, a route, an envelope shape, an engine setting
// (stash and mod scope live in the engine sections), and a setup field that must survive.
uint32_t dirtyEverything(Fixture& f) {
  const uint32_t midi = eg13::params::id(0, eg13::params::Slot::Part, uint32_t(eg13::PartParam::MidiChannel));
  auto j = nlohmann::json::parse(f.store.save(f.synth));
  j["params"][std::to_string(midi)] = 7;
  j["params"][std::to_string(kEngineA)] = 4;
  j["envelopeShapes"]["0"][0] = {{"count", 2}, {"types", {1, 1, 0, 0, 0, 0}}, {"loops", {0, 0, 0, 0, 0, 0}}};
  for (auto it = j.begin(); it != j.end(); ++it) {
    if (it.key() == "params" || it.key() == "routes" || it.key() == "envelopeShapes" || !it.value().is_object()) continue;
    it.value()["0"]["0"][0] = 0.123;
  }
  REQUIRE(f.ctx.loadState(j.dump()) != int(eg13::state::LoadResult::Invalid));
  REQUIRE(f.api.route_set(f.api.context, 0, 5, 1, 2, 0));
  f.dirty = 0;
  return midi;
}

// After preset_init the saved state equals a fresh synth's, except the setup fields.
void requireInitState(Fixture& f, eg13::PluginKind kind, uint32_t midi) {
  auto now = nlohmann::json::parse(f.store.save(f.synth));
  auto fresh = nlohmann::json::parse(eg13::state::StateStore().save(eg13::Synth(kind)));
  REQUIRE(now["params"][std::to_string(midi)] == 7);  // setup kept
  for (auto it = fresh["params"].begin(); it != fresh["params"].end(); ++it) {
    uint32_t id = uint32_t(std::stoul(it.key()));
    if (eg13::preset::isSetupParam(id)) continue;
    INFO("param " << it.key());
    REQUIRE(now["params"][it.key()] == it.value());
  }
  for (auto it = fresh.begin(); it != fresh.end(); ++it) {
    if (it.key() == "params") continue;
    INFO("section " << it.key());
    REQUIRE(now[it.key()] == it.value());
  }
}

}  // namespace

TEST_CASE("preset_init resets shapes, stashes and scopes and keeps setup fields") {
  Fixture f;
  const uint32_t midi = dirtyEverything(f);
  char message[128];
  REQUIRE(f.api.preset_init(f.api.context, message, sizeof message) == EG13_PRESET_OK);
  REQUIRE(f.dirty == 1);
  requireInitState(f, eg13::PluginKind::Instrument, midi);
}

TEST_CASE("Init is the instrument's declared host defaults, apart from setup fields") {
  // A new instrument opens on the first-load preset, so clap-validator's param-default-values is
  // skipped for it (scripts/validate.sh); this keeps the declared defaults tied to Init instead.
  Fixture f;
  dirtyEverything(f);
  REQUIRE(f.api.preset_init(f.api.context, nullptr, 0) == EG13_PRESET_OK);
  for (size_t i = 0; i < eg13::kParamCount; ++i) {
    const uint32_t id = eg13::paramSpec(i).id;
    if (eg13::preset::isSetupParam(id)) continue;
    INFO("param " << eg13::paramSpec(i).name);
    REQUIRE(f.synth.param(id) == double(eg13::paramDefault(i, eg13::PluginKind::Instrument)));
  }
}

TEST_CASE("EG13 FX preset_init restores the FX init patch") {
  Fixture f(eg13::PluginKind::Effect);
  const uint32_t midi = dirtyEverything(f);
  REQUIRE(f.api.preset_init(f.api.context, nullptr, 0) == EG13_PRESET_OK);
  requireInitState(f, eg13::PluginKind::Effect, midi);
  // The FX init differs from the instrument's: another source engine, route 0 at 20 %.
  REQUIRE(f.synth.param(kEngineA) != eg13::Synth(eg13::PluginKind::Instrument).param(kEngineA));
  EG13Route r{};
  REQUIRE(f.api.route_get(f.api.context, 0, 0, &r));
  REQUIRE(r.destination == 32);
  REQUIRE(r.amount == Catch::Approx(0.2f));
}
