#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "alloc_counter.h"
#include "mod/mod_routes.h"
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

TEST_CASE("A muted Part B reads zero while it plays") {
  Synth synth(PluginKind::Instrument);
  synth.prepare(kRate, kBlock);
  synth.setActive(true);
  UiSnapshot snap;
  synth.setParam(idOf(masterParamIndex(MasterParam::Mode)), double(PartMode::Layer));
  synth.noteOn(0, 60, 1.0f);  // both parts play
  runBlocks(synth, 100);
  synth.uiSnapshot(&snap);
  CHECK(snap.partPeak[1][0] > 0.0f);
  synth.setParam(idOf(masterParamIndex(MasterParam::Mode)), double(PartMode::Single));  // mutes B
  runBlocks(synth, 200);  // the mute ramp ends
  synth.uiSnapshot(&snap);  // resets the peaks
  runBlocks(synth, 5);
  synth.uiSnapshot(&snap);
  CHECK(snap.activeVoices[1] > 0);  // B is still playing
  CHECK(snap.partPeak[1][0] == 0.0f);
  CHECK(snap.partPeak[1][1] == 0.0f);
  CHECK(snap.partPeak[0][0] > 0.0f);
  synth.setActive(false);
}

TEST_CASE("Part peaks hold the maximum until read") {
  Synth synth(PluginKind::Instrument);
  synth.prepare(kRate, kBlock);
  synth.setActive(true);
  UiSnapshot snap;
  synth.noteOn(0, 60, 1.0f);
  runBlocks(synth, 100);
  synth.uiSnapshot(&snap);  // reset
  runBlocks(synth, 1);      // one loud block
  synth.allNotesOff();
  runBlocks(synth, 400);    // then silence; the last chunks read 0
  synth.uiSnapshot(&snap);
  CHECK(snap.partPeak[0][0] > 0.0f);
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

TEST_CASE("modValue shows the route's effect") {
  // The init patch: Mod 2 out 1 -> Filter P1 at +30 %.
  Synth synth(PluginKind::Instrument);
  synth.prepare(kRate, kBlock);
  synth.setActive(true);
  synth.noteOn(0, 60, 1.0f);
  runBlocks(synth, 20);  // the envelope has settled on its sustain
  UiSnapshot snap;
  synth.uiSnapshot(&snap);
  int voice = -1;
  float env = 0.0f;
  for (int v = 0; v < 8; ++v) {
    const float out = synth.part(0).modulation().outputs(1, v)[0];
    if (out > env) {
      env = out;
      voice = v;
    }
  }
  REQUIRE(voice >= 0);
  REQUIRE(env > 0.0f);
  const float base = snap.values[filterParamIndex(0, 0)];
  const float expected = std::clamp(base + env * 0.3f, 0.0f, 1.0f);
  CHECK(snap.modValue[0][kDestFilterP + 0] == Catch::Approx(expected).margin(1e-3));
  CHECK(snap.modValue[0][kDestFilterP + 0] != Catch::Approx(base).margin(1e-3));
  CHECK(snap.modValue[0][kDestFilterP + 1] == Catch::Approx(snap.values[filterParamIndex(0, 1)]).margin(1e-3));
  CHECK(snap.modValue[0][kDestPitch] == Catch::Approx(0.5f).margin(1e-3));  // the mod wheel is down
  CHECK(snap.modValue[0][kDestPan] == Catch::Approx(0.5f).margin(1e-6));
  synth.setActive(false);
}

TEST_CASE("per-part destinations use the part value") {
  Synth synth(PluginKind::Instrument);
  synth.prepare(kRate, kBlock);
  synth.setActive(true);
  synth.setRoute(0, 5, Route{kSrcGate, kDestPartLevel, kSrcNone});
  synth.setParam(paramSpec(routeAmountParamIndex(0, 5)).id, -0.25);
  synth.noteOn(0, 60, 1.0f);
  runBlocks(synth, 10);
  UiSnapshot snap;
  synth.uiSnapshot(&snap);
  const float level = snap.values[partParamIndex(0, PartParam::Level)];
  CHECK(snap.modValue[0][kDestPartLevel] == Catch::Approx(std::clamp(level - 0.25f, 0.0f, 1.0f)).margin(1e-3));
  CHECK(snap.modValue[0][kDestPartLevel] < level);
  synth.setActive(false);
}

TEST_CASE("mod output min and max are read and reset") {
  Synth synth(PluginKind::Instrument);
  synth.prepare(kRate, kBlock);
  synth.setActive(true);
  UiSnapshot snap;
  synth.uiSnapshot(&snap);  // nothing has run: no data
  CHECK(std::isinf(snap.modOutMin[0][1][0]));
  CHECK(snap.modOutMin[0][1][0] > 0.0f);
  CHECK(snap.modOutMax[0][1][0] < 0.0f);
  synth.noteOn(0, 60, 1.0f);
  runBlocks(synth, 20);
  synth.uiSnapshot(&snap);
  CHECK(snap.modOutMin[0][1][0] <= snap.modOutMax[0][1][0]);
  CHECK(snap.modOutMax[0][1][0] > 0.0f);  // Mod 2's envelope rose
  CHECK(snap.modOutMin[0][1][0] < snap.modOutMax[0][1][0]);  // from silence, within the window
  synth.uiSnapshot(&snap);  // read and reset, no audio in between
  CHECK(snap.modOutMin[0][1][0] > snap.modOutMax[0][1][0]);
  CHECK(std::isinf(snap.modOutMin[0][1][0]));
  runBlocks(synth, 1);
  synth.uiSnapshot(&snap);
  CHECK(snap.modOutMin[0][1][0] <= snap.modOutMax[0][1][0]);  // sampling resumes
  synth.setActive(false);
}

TEST_CASE("unused destinations are NaN") {
  Synth synth(PluginKind::Instrument);
  synth.prepare(kRate, kBlock);
  synth.setActive(true);
  synth.noteOn(0, 60, 1.0f);
  runBlocks(synth, 5);
  UiSnapshot snap;
  synth.uiSnapshot(&snap);
  for (int p = 0; p < kNumParts; ++p) {
    CHECK(std::isnan(snap.modValue[p][kDestNone]));
    for (int d = 5; d < kDestSourceP; ++d) CHECK(std::isnan(snap.modValue[p][d]));
    CHECK(std::isfinite(snap.modValue[p][kDestPitch]));
    CHECK(std::isfinite(snap.modValue[p][kDestPartLevel]));
  }
  // Init patch: Mod 4 is Off, so its P's have no value; Mod 1 runs.
  int off = 0;
  for (int s = 0; s < kNumModSlots; ++s) {
    if (snap.values[modEngineParamIndex(0, s)] < 0.5f) {
      ++off;
      CHECK(std::isnan(snap.modValue[0][modParamDestination(s, 0)]));
    } else {
      CHECK(std::isfinite(snap.modValue[0][modParamDestination(s, 0)]));
    }
  }
  CHECK(off >= 1);
  synth.setActive(false);
}

TEST_CASE("publishing the modulation allocates nothing") {
  Synth synth(PluginKind::Instrument);
  synth.prepare(kRate, kBlock);
  synth.setActive(true);
  synth.noteOn(0, 60, 1.0f);
  runBlocks(synth, 5);
  std::vector<float> l(kBlock), r(kBlock);
  float* out[2] = {l.data(), r.data()};
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int b = 0; b < 50; ++b) {
      synth.beginBlock();
      synth.process(nullptr, out, nullptr, kBlock);
    }
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
  synth.setActive(false);
}

TEST_CASE("modValue Pitch is semitones / 48 + 0.5") {
  Synth synth(PluginKind::Instrument);
  synth.prepare(kRate, kBlock);
  synth.setActive(true);
  synth.setRoute(0, 6, Route{kSrcGate, kDestPitch, kSrcNone});  // gate is 1 while held
  synth.setParam(paramSpec(routeAmountParamIndex(0, 6)).id, 12.0 / kPitchRangeSemitones);
  synth.noteOn(0, 60, 1.0f);
  runBlocks(synth, 10);
  UiSnapshot snap;
  synth.uiSnapshot(&snap);
  CHECK(snap.modValue[0][kDestPitch] == Catch::Approx(12.0f / 48.0f + 0.5f).margin(1e-3));
  synth.setActive(false);
}

TEST_CASE("modValue follows the last-started voice") {
  Synth synth(PluginKind::Instrument);
  synth.prepare(kRate, kBlock);
  synth.setActive(true);
  // Pitch follows the key (Note source, keytrack (note - 60) / 120, amount 1 = 24 semitones).
  synth.setRoute(0, 6, Route{kSrcNote, kDestPitch, kSrcNone});
  synth.setParam(paramSpec(routeAmountParamIndex(0, 6)).id, 1.0);
  synth.noteOn(0, 72, 1.0f);
  runBlocks(synth, 5);
  synth.noteOn(0, 48, 1.0f);  // the second note, a different voice
  runBlocks(synth, 10);
  UiSnapshot snap;
  synth.uiSnapshot(&snap);
  const float second = (48.0f - 60.0f) / 120.0f * 24.0f / 48.0f + 0.5f;
  const float first = (72.0f - 60.0f) / 120.0f * 24.0f / 48.0f + 0.5f;
  CHECK(snap.modValue[0][kDestPitch] == Catch::Approx(second).margin(1e-3));
  CHECK(snap.modValue[0][kDestPitch] != Catch::Approx(first).margin(1e-3));
  synth.setActive(false);
}
