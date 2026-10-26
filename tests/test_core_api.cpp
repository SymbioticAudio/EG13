#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>

#include "core_api.h"
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
  Fixture() : bridge(synth, {}), ctx{synth, bridge, {}}, api(eg13::makeCoreApi(ctx)) {}
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
  eg13::Synth synth{eg13::PluginKind::Instrument};
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
  REQUIRE(dest(0, Slot::Part, 1) == 0);
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
