#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <string>

#include "core_api.h"
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
