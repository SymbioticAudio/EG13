#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <vector>

#include "alloc_counter.h"
#include "engine/engine_registry.h"
#include "engine/marbles_engine.h"
#include "mod/part_modulation.h"
#include "test_helpers.h"
#include "test_mod_engines.h"

using namespace eg13;

namespace {

struct Rig {
  PartModulation mod{test::testModEngines};
  ModParams params;
  ModPartInputs part;
  ModVoiceInputs voices[PartModulation::kVoices];
  SlotParams base;
  Rig() {
    mod.prepare(48000.0);
    for (auto& v : voices) v.active = true;
  }
  void route(int r, uint16_t source, uint16_t destination, float amount, uint16_t via = kSrcNone) {
    params.routes.route[r] = {source, destination, via};
    params.routes.amount[r] = amount;
  }
  VoiceControl tick(int v = 0) {
    mod.processPart(params, part, voices, 16);
    VoiceControl c;
    mod.processVoice(v, params, part, voices[v], base, &c, 16);
    part.retrigger = false;
    voices[v].retrigger = false;
    return c;
  }
};

}  // namespace

TEST_CASE("Routes into mod-slot P's read the previous tick whatever the slot order and scope") {
  for (int scopes = 0; scopes < 4; ++scopes) {
    const bool stepPerPart = (scopes & 1) != 0;
    const bool echoPerPart = (scopes & 2) != 0;
    for (int order = 0; order < 2; ++order) {
      Rig rig;
      rig.part.lastVoice = 0;  // per-part slots read voice 0's per-voice slots
      const int step = order == 0 ? 0 : 3;
      const int echo = order == 0 ? 1 : 0;
      rig.mod.configure(step, test::kTestStep, stepPerPart);
      rig.mod.configure(echo, test::kTestEcho, echoPerPart);
      rig.route(0, modOutputSource(step, 0), modParamDestination(echo, 0), 1.0f);
      for (int k = 1; k <= 20; ++k) {
        rig.tick();
        INFO("step per part " << stepPerPart << " echo per part " << echoPerPart << " order " << order
                              << " tick " << k);
        REQUIRE(rig.mod.outputs(step, stepPerPart ? -1 : 0)[0] == 0.01f * float(k));
        REQUIRE(std::fabs(rig.mod.outputs(echo, echoPerPart ? -1 : 0)[0] - 0.01f * float(k - 1)) < 1e-6f);
      }
    }
  }
}

TEST_CASE("A mod slot routed into its own P settles and stays bounded") {
  Rig rig;
  rig.mod.configure(0, test::kTestEcho, false);
  rig.params.slot[0].p[0] = 0.2f;
  rig.route(0, modOutputSource(0, 0), modParamDestination(0, 0), 0.5f);  // x' = 0.2 + 0.5 x
  for (int k = 0; k < 100; ++k) {
    rig.tick();
    REQUIRE(rig.mod.outputs(0, 0)[0] <= 1.0f);
  }
  REQUIRE(std::fabs(rig.mod.outputs(0, 0)[0] - 0.4f) < 1e-5f);
  rig.params.routes.amount[0] = 1.0f;  // x' = 0.2 + x: grows until the clamp
  for (int k = 0; k < 100; ++k) rig.tick();
  REQUIRE(rig.mod.outputs(0, 0)[0] == 1.0f);
}

TEST_CASE("Voice controls read this tick's mod outputs") {
  Rig rig;
  rig.mod.configure(0, test::kTestStep, false);
  rig.base.filter.p[0] = 0.5f;
  rig.route(0, modOutputSource(0, 0), uint16_t(kDestFilterP + 0), 1.0f);
  for (int k = 1; k <= 10; ++k) {
    const VoiceControl c = rig.tick();
    REQUIRE(std::fabs(c.params.filter.p[0] - (0.5f + 0.01f * float(k))) < 1e-6f);
    REQUIRE(c.params.filter.p[1] == rig.base.filter.p[1]);
  }
}

TEST_CASE("Per-part slots run once per tick, see the part's gate and are shared by every voice") {
  Rig rig;
  rig.mod.configure(1, test::kTestStep, true);
  rig.base.source.p[2] = 0.25f;
  rig.route(0, modOutputSource(1, 1), uint16_t(kDestSourceP + 2), 0.5f);  // the part gate
  rig.part.gate = true;
  rig.part.retrigger = true;
  rig.mod.processPart(rig.params, rig.part, rig.voices, 16);
  VoiceControl a, b;
  rig.mod.processVoice(0, rig.params, rig.part, rig.voices[0], rig.base, &a, 16);
  rig.mod.processVoice(5, rig.params, rig.part, rig.voices[5], rig.base, &b, 16);
  REQUIRE(rig.mod.outputs(1, -1)[0] == 0.01f);  // one tick, not one per voice
  REQUIRE(rig.mod.outputs(1, -1)[2] == 0.1f);   // one retrigger
  REQUIRE(a.params.source.p[2] == 0.75f);
  REQUIRE(b.params.source.p[2] == 0.75f);
  REQUIRE(rig.mod.outputs(1, 0)[0] == 0.0f);  // the per-voice instance did not run
}

TEST_CASE("Per-voice slots see their own voice's gate and retrigger") {
  Rig rig;
  rig.mod.configure(0, test::kTestStep, false);
  rig.voices[2].gate = true;
  rig.voices[2].retrigger = true;
  VoiceControl c;
  rig.mod.processPart(rig.params, rig.part, rig.voices, 16);
  rig.mod.processVoice(2, rig.params, rig.part, rig.voices[2], rig.base, &c, 16);
  rig.mod.processVoice(3, rig.params, rig.part, rig.voices[3], rig.base, &c, 16);
  REQUIRE(rig.mod.outputs(0, 2)[1] == 1.0f);
  REQUIRE(rig.mod.outputs(0, 2)[2] == 0.1f);
  REQUIRE(rig.mod.outputs(0, 3)[1] == 0.0f);
  REQUIRE(rig.mod.outputs(0, 3)[2] == 0.0f);
  rig.mod.startVoice(2);
  REQUIRE(rig.mod.outputs(0, 2)[0] == 0.0f);
  rig.mod.processVoice(2, rig.params, rig.part, rig.voices[2], rig.base, &c, 16);
  REQUIRE(rig.mod.outputs(0, 2)[0] == 0.01f);  // the tick count started again
}

TEST_CASE("Pitch, Amp and Pan follow their routes") {
  Rig rig;
  rig.voices[0].velocity = 0.8f;
  rig.voices[0].bend = 1.5f;
  rig.part.modWheel = 0.5f;
  VoiceControl c = rig.tick();
  REQUIRE_FALSE(c.ampRouted);
  REQUIRE(c.pitch == 1.5f);  // the per-note bend alone
  REQUIRE(c.pan == 0.0f);
  rig.route(0, kSrcVelocity, kDestPitch, 0.5f, kSrcModWheel);   // 0.8 * 0.5 * 0.5 = 0.2 -> 4.8 st
  rig.route(1, kSrcVelocity, kDestAmp, 1.0f);
  rig.route(2, kSrcVelocity, kDestAmp, 1.0f);                   // 1.6, clamped to 1
  rig.route(3, kSrcModWheel, kDestPan, -0.5f);                  // 0.5 - 0.25 -> -0.5
  c = rig.tick();
  REQUIRE(std::fabs(c.pitch - (0.2f * 24.0f + 1.5f)) < 1e-5f);
  REQUIRE(c.ampRouted);
  REQUIRE(c.amp == 1.0f);
  REQUIRE(std::fabs(c.pan + 0.5f) < 1e-6f);
  rig.params.routes.amount[1] = 0.0f;
  rig.params.routes.amount[2] = 0.0f;
  c = rig.tick();
  REQUIRE(c.ampRouted);  // routed at zero: silent, not open
  REQUIRE(c.amp == 0.0f);
}

TEST_CASE("Pitch routes sum to at most 48 semitones") {
  Rig rig;
  rig.voices[0].velocity = 1.0f;
  rig.voices[0].bend = 1.5f;
  for (int r = 0; r < 3; ++r) rig.route(r, kSrcVelocity, kDestPitch, 1.0f);  // 3 x 24 = 72 st
  VoiceControl c = rig.tick();
  REQUIRE(c.pitch == 48.0f + 1.5f);  // the per-note bend comes on top of the cap
  for (int r = 0; r < 3; ++r) rig.params.routes.amount[r] = -1.0f;
  c = rig.tick();
  REQUIRE(c.pitch == -48.0f + 1.5f);
}

TEST_CASE("Fixed sources: keytrack, note bend, slide, release velocity") {
  Rig rig;
  rig.voices[0].note = 72.0f;
  rig.voices[0].bend = 24.0f;
  rig.voices[0].slide = 0.25f;
  rig.voices[0].releaseVelocity = 0.6f;
  rig.part.slide = 0.5f;
  rig.part.noteBendRange = 48.0f;
  rig.mod.configure(0, test::kTestEcho, false);
  rig.route(0, kSrcNote, modParamDestination(0, 0), 1.0f);
  rig.route(1, kSrcNoteBend, modParamDestination(0, 1), 1.0f);
  rig.route(2, kSrcSlide, modParamDestination(0, 2), 1.0f);
  rig.route(3, kSrcReleaseVelocity, modParamDestination(0, 3), 1.0f);
  rig.tick();
  const float* out = rig.mod.outputs(0, 0);
  REQUIRE(out[0] == 0.1f);   // (72 - 60) / 120: 100 % tracks an octave per octave on the cutoff
  REQUIRE(out[1] == 0.5f);   // 24 of 48 semitones
  REQUIRE(out[2] == 0.5f);   // the larger of the part's and the voice's slide
  REQUIRE(out[3] == 0.6f);
}

TEST_CASE("Part-level routes read the most recently started voice") {
  Rig rig;
  rig.voices[3].velocity = 0.6f;
  rig.part.lastVoice = 3;
  rig.route(0, kSrcVelocity, kDestPartLevel, -0.5f);
  rig.mod.processPart(rig.params, rig.part, rig.voices, 16);
  REQUIRE(std::fabs(rig.mod.partLevel(rig.params, 0.8f) - 0.5f) < 1e-6f);
  rig.part.lastVoice = -1;
  rig.mod.processPart(rig.params, rig.part, rig.voices, 16);
  REQUIRE(rig.mod.partLevel(rig.params, 0.8f) == 0.8f);
}

TEST_CASE("A NaN from a mod engine contributes nothing") {
  Rig rig;
  rig.mod.configure(0, test::kTestNan, false);
  rig.base.shaper.p[0] = 0.3f;
  rig.route(0, modOutputSource(0, 0), uint16_t(kDestShaperP + 0), 1.0f);
  rig.route(1, modOutputSource(0, 0), kDestPitch, 1.0f);
  const VoiceControl c = rig.tick();
  REQUIRE(c.params.shaper.p[0] == 0.3f);
  REQUIRE(c.pitch == 0.0f);
}

TEST_CASE("Changing a slot's engine or scope restarts the instances it starts using") {
  Rig rig;
  rig.mod.configure(0, test::kTestStep, false);
  for (int k = 0; k < 5; ++k) rig.tick();
  REQUIRE(rig.mod.outputs(0, 0)[0] == 0.01f * 5.0f);
  rig.mod.configure(0, test::kTestStep, true);
  rig.tick();
  REQUIRE(rig.mod.outputs(0, -1)[0] == 0.01f);
  rig.mod.configure(0, test::kTestStep, false);
  rig.tick();
  REQUIRE(rig.mod.outputs(0, 0)[0] == 0.01f);
  rig.mod.configure(0, test::kTestEcho, false);
  REQUIRE(rig.mod.engine(0) == test::kTestEcho);
  rig.mod.configure(0, 77, false);
  REQUIRE(rig.mod.engine(0) == test::kTestNan);  // clamped to the last engine
}

TEST_CASE("Part modulation with the real engines does not allocate") {
  PartModulation mod;
  mod.setSeedBase(100);
  mod.prepare(48000.0);
  ModParams params;
  for (auto& slot : params.slot) {
    for (float& v : slot.p) v = 0.5f;
  }
  params.routes.route[0] = {modOutputSource(0, 0), kDestAmp, kSrcVelocity};
  params.routes.route[1] = {modOutputSource(1, 0), uint16_t(kDestFilterP), kSrcNone};
  params.routes.route[2] = {modOutputSource(2, 0), modParamDestination(3, 2), kSrcNone};
  params.routes.route[3] = {modOutputSource(3, 3), kDestPitch, kSrcModWheel};
  for (float& a : params.routes.amount) a = 0.5f;
  ModPartInputs part;
  ModVoiceInputs voices[PartModulation::kVoices];
  for (auto& v : voices) {
    v.active = true;
    v.gate = true;
    v.velocity = 0.7f;
  }
  const SlotParams base;
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int t = 0; t < 600; ++t) {
      if (t % 150 == 0) {
        for (int s = 0; s < kNumModSlots; ++s) mod.configure(s, (t / 150 + s) % 5, s == 3);
      }
      part.beat = double(t) * 0.01;
      mod.processPart(params, part, voices, 16);
      for (int v = 0; v < PartModulation::kVoices; ++v) {
        VoiceControl c;
        mod.processVoice(v, params, part, voices[v], base, &c, 16);
      }
      mod.partLevel(params, 0.5f);
    }
    mod.startVoice(3);
    mod.reset();
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}

namespace {

double correlation(const std::vector<float>& a, const std::vector<float>& b) {
  double ma = 0, mb = 0;
  for (size_t i = 0; i < a.size(); ++i) {
    ma += a[i];
    mb += b[i];
  }
  ma /= double(a.size());
  mb /= double(b.size());
  double ab = 0, aa = 0, bb = 0;
  for (size_t i = 0; i < a.size(); ++i) {
    ab += (a[i] - ma) * (b[i] - mb);
    aa += (a[i] - ma) * (a[i] - ma);
    bb += (b[i] - mb) * (b[i] - mb);
  }
  return ab / std::sqrt(aa * bb);
}

// X1 of Random / S&H, per voice in slots 0 and 1, sampled each time slot 0 voice 0 takes a new
// value: [0] slot 0 voice 0, [1] slot 0 voice 1, [2] slot 1 voice 0.
std::vector<std::vector<float>> randomX1(uint32_t seedBase, size_t count) {
  PartModulation mod;
  mod.setSeedBase(seedBase);
  mod.prepare(48000.0);
  const int random = int(ModEngineKind::Random);
  mod.configure(0, random, false);
  mod.configure(1, random, false);
  ModParams params;
  for (auto& slot : params.slot) {
    slot = test::defaultParams(MarblesEngine::staticInfo().engine);
    slot.p[2] = 1.0f;               // T Rate: fastest
    slot.p[9] = modOptionValue(2);  // T Range: 4x
  }
  ModPartInputs part;
  ModVoiceInputs voices[PartModulation::kVoices];
  for (auto& v : voices) v.active = true;
  const SlotParams base;
  constexpr int kX1 = 3;
  std::vector<std::vector<float>> x(3);
  float previous = mod.outputs(0, 0)[kX1];
  while (x[0].size() < count) {
    mod.processPart(params, part, voices, 16);
    VoiceControl c;
    mod.processVoice(0, params, part, voices[0], base, &c, 16);
    mod.processVoice(1, params, part, voices[1], base, &c, 16);
    const float value = mod.outputs(0, 0)[kX1];
    if (value == previous) continue;
    previous = value;
    x[0].push_back(value);
    x[1].push_back(mod.outputs(0, 1)[kX1]);
    x[2].push_back(mod.outputs(1, 0)[kX1]);
  }
  return x;
}

}  // namespace

TEST_CASE("Every mod slot instance has its own random sequence, the same each time") {
  // Each (seed base, slot, voice) seed is hashed (Marbles' generator is a bare LCG, so
  // neighbouring seeds would give related sequences). Two voices of a slot, and one voice in two
  // slots, must not follow each other.
  const auto x = randomX1(100, 1000);
  const double voices01 = correlation(x[0], x[1]);
  const double slots01 = correlation(x[0], x[2]);
  INFO("voices " << voices01 << " slots " << slots01);
  REQUIRE(std::fabs(voices01) < 0.2);
  REQUIRE(std::fabs(slots01) < 0.2);
  REQUIRE(randomX1(100, 50)[0] == std::vector<float>(x[0].begin(), x[0].begin() + 50));  // deterministic
  REQUIRE(randomX1(101, 50)[0] != std::vector<float>(x[0].begin(), x[0].begin() + 50));  // the base matters
}
