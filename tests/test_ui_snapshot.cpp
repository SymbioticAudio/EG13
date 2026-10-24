#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "alloc_counter.h"
#include "synth/param_table.h"
#include "synth/synth.h"
#include "synth/ui_snapshot.h"

using namespace eg13;

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

void runBlocks(Synth& synth, int blocks) {
  std::vector<float> l(kBlock), r(kBlock);
  float* out[2] = {l.data(), r.data()};
  for (int b = 0; b < blocks; ++b) {
    synth.beginBlock();
    synth.process(nullptr, out, nullptr, kBlock);
  }
}

uint32_t idOf(size_t index) { return paramSpec(index).id; }

}  // namespace

TEST_CASE("Part peaks report each part's output peak and reset on read") {
  Synth synth(PluginKind::Instrument);
  synth.prepare(kRate, kBlock);
  synth.setActive(true);
  UiSnapshot snap;
  synth.noteOn(0, 60, 1.0f);  // Single mode: Part A only
  runBlocks(synth, 100);
  synth.uiSnapshot(&snap);
  CHECK(snap.partPeak[0][0] > 0.0f);
  CHECK(snap.partPeak[0][1] > 0.0f);
  CHECK(snap.partPeak[1][0] == 0.0f);
  CHECK(snap.partPeak[1][1] == 0.0f);
  CHECK(snap.activeVoices[0] > 0);
  CHECK(snap.activeVoices[1] == 0);
  synth.uiSnapshot(&snap);  // no audio in between
  CHECK(snap.partPeak[0][0] == 0.0f);
  CHECK(snap.partPeak[0][1] == 0.0f);
  synth.setActive(false);
}

TEST_CASE("Snapshot values follow setParam and loadParams") {
  Synth synth(PluginKind::Instrument);
  synth.prepare(kRate, kBlock);
  UiSnapshot snap;
  const size_t level = masterParamIndex(MasterParam::Level);
  const size_t pan = partParamIndex(1, PartParam::Pan);
  synth.setParam(idOf(level), 0.25);
  synth.uiSnapshot(&snap);
  CHECK(snap.values[level] == 0.25f);
  ParamSnapshot values = synth.defaultSnapshot();
  values[pan] = -0.5f;
  values[level] = 0.75f;
  synth.loadParams(values);
  synth.uiSnapshot(&snap);
  CHECK(snap.values[pan] == -0.5f);
  CHECK(snap.values[level] == 0.75f);
  synth.setActive(true);  // a load while active shows at once, before the audio applies it
  values[level] = 0.5f;
  synth.loadParams(values);
  synth.uiSnapshot(&snap);
  CHECK(snap.values[level] == 0.5f);
  runBlocks(synth, 1);
  synth.uiSnapshot(&snap);
  CHECK(snap.values[level] == 0.5f);
  synth.setActive(false);
}

TEST_CASE("Engine generation changes on every engine switch, including state load") {
  Synth synth(PluginKind::Instrument);
  synth.prepare(kRate, kBlock);
  synth.setActive(true);
  UiSnapshot snap;
  synth.uiSnapshot(&snap);
  uint32_t last = snap.engineGeneration;
  auto changed = [&] {
    synth.uiSnapshot(&snap);
    const bool c = snap.engineGeneration != last;
    last = snap.engineGeneration;
    return c;
  };
  CHECK_FALSE(changed());
  synth.setParam(idOf(sourceParamIndex(0, 0)), 0.3);  // a plain P is not an engine switch
  CHECK_FALSE(changed());
  synth.setParam(idOf(sourceEngineParamIndex(0)), 1);  // UI or host selector change
  CHECK(changed());
  synth.setParam(idOf(filterEngineParamIndex(1)), 2);
  CHECK(changed());
  synth.setParam(idOf(shaperEngineParamIndex(0)), 1);
  CHECK(changed());
  synth.setParam(idOf(sourceEngineParamIndex(0)), 1);  // same engine: nothing switches
  CHECK_FALSE(changed());
  synth.loadParams(synth.defaultSnapshot());  // state load
  CHECK(changed());
  runBlocks(synth, 1);
  CHECK(changed());  // and again once the audio thread has adopted the engines
  synth.setActive(false);
}

TEST_CASE("uiSnapshot allocates nothing") {
  Synth synth(PluginKind::Instrument);
  synth.prepare(kRate, kBlock);
  synth.setActive(true);
  synth.noteOn(0, 60, 1.0f);
  runBlocks(synth, 10);
  UiSnapshot snap;
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int i = 0; i < 20; ++i) synth.uiSnapshot(&snap);
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
  synth.setActive(false);
}
