#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "alloc_counter.h"
#include "engine/engine_registry.h"
#include "mod/mod_factory.h"
#include "mod/mod_slot.h"
#include "test_helpers.h"
#include "test_mod_engines.h"

using namespace eg13;

namespace {

std::vector<float> lfoRun(ModSlot& slot, int ticks) {
  const ParamValues p = test::defaultParams(modEngineInfo(int(ModEngineKind::Lfo)).engine);
  std::vector<float> values;
  for (int t = 0; t < ticks; ++t) {
    slot.process(ModContext{}, p, EnvelopeShape{}, 16);
    values.push_back(slot.outputs()[0]);
  }
  return values;
}

}  // namespace

TEST_CASE("The mod engine registry names every engine and falls back to Off") {
  const char* const ids[kNumModEngines] = {"off", "stages", "tides", "marbles", "peaks"};
  const char* const names[kNumModEngines] = {"Off", "Envelope", "LFO", "Random / S&H", "Function Generator"};
  for (int e = 0; e < kNumModEngines; ++e) {
    REQUIRE(std::string(modEngineInfo(e).engine.id) == ids[e]);
    REQUIRE(std::string(modEngineInfo(e).engine.displayName) == names[e]);
    REQUIRE(modEngineInfo(e).engine.paramCount <= kModParamCount);
    REQUIRE(modEngineInfo(e).outputCount <= kMaxModOutputs);
    REQUIRE(std::string(makeModEngine(e)->info().engine.id) == ids[e]);
  }
  REQUIRE(modEngineFromSelector(3.0) == 3);
  REQUIRE(modEngineFromSelector(9.0) == 0);
  REQUIRE(modEngineFromSelector(std::nan("")) == 0);
  REQUIRE(std::string(modEngineInfo(17).engine.id) == "off");
  REQUIRE(makeAllModEngines().size() == size_t(kNumModEngines));
  REQUIRE(modEngineInfo(int(ModEngineKind::Random)).perPartByDefault);
  REQUIRE_FALSE(modEngineInfo(int(ModEngineKind::Lfo)).perPartByDefault);
}

TEST_CASE("A mod slot starts Off and outputs zeros") {
  ModSlot slot(makeAllModEngines());
  slot.prepare(48000.0);
  REQUIRE(slot.engine() == 0);
  slot.process(ModContext{}, ParamValues{}, EnvelopeShape{}, 16);
  for (int k = 0; k < kMaxModOutputs; ++k) REQUIRE(slot.outputs()[k] == 0.0f);
}

TEST_CASE("Changing a slot's engine starts the new engine from reset") {
  ModSlot slot(makeAllModEngines());
  slot.prepare(48000.0);
  slot.setEngine(int(ModEngineKind::Lfo));
  const auto fresh = lfoRun(slot, 200);
  slot.setEngine(int(ModEngineKind::Envelope));
  slot.process(ModContext{}, ParamValues{}, EnvelopeShape{}, 16);
  slot.setEngine(int(ModEngineKind::Lfo));
  REQUIRE(lfoRun(slot, 200) == fresh);
  slot.setEngine(99);
  REQUIRE(slot.engine() == kNumModEngines - 1);
}

TEST_CASE("A slot whose engine outputs NaN resets it and reads zero") {
  std::vector<std::unique_ptr<ModEngine>> engines;
  engines.push_back(std::make_unique<OffModEngine>());
  engines.push_back(std::make_unique<test::NanModEngine>());
  auto* nan = static_cast<test::NanModEngine*>(engines[1].get());
  ModSlot slot(std::move(engines));
  slot.prepare(48000.0);
  slot.setEngine(1);
  const int resetsBefore = nan->resets;
  slot.process(ModContext{}, ParamValues{}, EnvelopeShape{}, 16);
  REQUIRE(slot.outputs()[0] == 0.0f);
  REQUIRE(slot.nanResets() == 1);
  REQUIRE(nan->resets == resetsBefore + 1);
  slot.process(ModContext{}, ParamValues{}, EnvelopeShape{}, 16);
  REQUIRE(slot.outputs()[0] == 0.25f);
}

TEST_CASE("The slot passes the envelope shape to its engine") {
  ModSlot slot(makeAllModEngines());
  slot.prepare(48000.0);
  slot.setEngine(int(ModEngineKind::Envelope));
  ParamValues p = test::defaultParams(modEngineInfo(int(ModEngineKind::Envelope)).engine);
  p.p[0] = modOptionValue(3);  // Custom
  // One looping Step segment with the gate patched: the module's sample and hold, which takes
  // its level when the gate rises (segment_generator.cc process_fn_table_).
  EnvelopeShape sampleAndHold;
  sampleAndHold.count = 1;
  sampleAndHold.type[0] = EnvelopeShape::Step;
  sampleAndHold.loop[0] = 1;
  p.p[1] = 0.4f;  // the level to sample
  p.p[2] = 0.0f;  // no glide
  ModContext ctx;
  ctx.gate = true;
  for (int t = 0; t < 100; ++t) slot.process(ctx, p, sampleAndHold, 16);
  REQUIRE(std::fabs(slot.outputs()[0] - 0.4f) < 1e-3f);
}

TEST_CASE("Mod slots switch and process without allocating") {
  ModSlot slot(makeAllModEngines());
  slot.prepare(44100.0);
  ParamValues p;
  for (float& v : p.p) v = 0.3f;
  ModContext ctx;
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int t = 0; t < 5000; ++t) {
      if (t % 250 == 0) slot.setEngine((t / 250) % kNumModEngines);
      ctx.gate = (t / 60) % 2 == 0;
      slot.process(ctx, p, EnvelopeShape{}, 1 + t % 16);
    }
    slot.reset();
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}
