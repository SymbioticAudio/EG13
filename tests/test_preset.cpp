#include <catch2/catch_test_macros.hpp>

#include <random>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "params/param_ids.h"
#include "preset/preset.h"
#include "state/state.h"
#include "synth/param_table.h"
#include "synth/synth.h"

using namespace eg13;

namespace {

uint32_t partId(int part, PartParam p) { return paramSpec(partParamIndex(part, p)).id; }
uint32_t masterId(MasterParam m) { return paramSpec(masterParamIndex(m)).id; }

// Random values for every parameter the filter accepts, and random routes and envelope shapes.
template <class Pick>
void randomise(Synth& s, std::mt19937& rng, Pick pick) {
  std::uniform_real_distribution<double> unit(0.0, 1.0);
  for (size_t i = 0; i < kParamCount; ++i) {
    const ParamSpec& spec = paramSpec(i);
    if (!pick(spec.id)) continue;
    s.setParam(spec.id, spec.minValue + unit(rng) * (spec.maxValue - spec.minValue));
  }
  for (int part = 0; part < kNumParts; ++part) {
    for (int r = 0; r < kNumRoutes; ++r) {
      if (!pick(params::id(uint32_t(part), params::Slot::Matrix, uint32_t(r + 1)))) continue;
      s.setRoute(part, r, Route{uint16_t(1 + rng() % 5), uint16_t(1 + rng() % 8), kSrcNone});
    }
    for (int slot = 0; slot < kNumModSlots; ++slot) {
      EnvelopeShape shape;
      shape.count = uint8_t(2 + rng() % 4);
      shape.type[1] = uint8_t(rng() % 3);
      s.setEnvelopeShape(part, slot, shape);
    }
  }
}

void makeSetupNonDefault(Synth& s, double offset) {
  for (int part = 0; part < kNumParts; ++part) {
    s.setParam(partId(part, PartParam::MidiChannel), 3 + part + offset);
    s.setParam(partId(part, PartParam::Output), 1);
    s.setParam(partId(part, PartParam::MpeMode), 1 + part);
    s.setParam(partId(part, PartParam::MpeBendRange), 12 + offset);
    s.setParam(partId(part, PartParam::CcA), 20 + offset);
    s.setParam(partId(part, PartParam::CcB), 21 + offset);
    s.setParam(partId(part, PartParam::CcC), 22 + offset);
    s.setParam(partId(part, PartParam::CcD), 23 + offset);
  }
  s.setParam(masterId(MasterParam::Level), 0.37 + offset * 0.1);
  s.setParam(masterId(MasterParam::VoiceGain), 0.41 + offset * 0.1);
  s.setParam(masterId(MasterParam::LatencyMode), 1);
}

preset::Meta meta(preset::Kind kind, PluginKind plugin = PluginKind::Instrument) {
  preset::Meta m;
  m.kind = kind;
  m.plugin = plugin;
  m.name = "Test";
  return m;
}

// Applies a preset the way the plugin will: merge, then StateStore::load.
state::LoadResult apply(Synth& s, state::StateStore& store, const std::string& presetJson, int part,
                        preset::ApplyResult* result = nullptr) {
  preset::ApplyResult local;
  preset::ApplyResult& r = result ? *result : local;
  const std::string merged = preset::mergeForApply(store.save(s), presetJson, s.kind(), part, r);
  if (merged.empty()) return r.result;
  return store.load(s, merged);
}

// Synth is not copyable: keep what the tests compare.
struct Snap {
  std::vector<double> params;
  PatchData patch;
  explicit Snap(const Synth& s) : patch(s.patch()) {
    for (size_t i = 0; i < kParamCount; ++i) params.push_back(s.param(paramSpec(i).id));
  }
  double at(uint32_t id) const { return params[size_t(paramIndex(id))]; }
};

}  // namespace

TEST_CASE("A Performance preset round-trips the sound of both parts") {
  std::mt19937 rng(7);
  Synth a(PluginKind::Instrument);
  randomise(a, rng, [](uint32_t) { return true; });
  state::StateStore storeA;
  const std::string file = preset::save(a, storeA, meta(preset::Kind::Performance), -1);
  REQUIRE_FALSE(file.empty());

  Synth b(PluginKind::Instrument);
  state::StateStore storeB;
  REQUIRE(b.patch().routes != a.patch().routes);  // the randomising did something
  REQUIRE(apply(b, storeB, file, 0) != state::LoadResult::Invalid);
  for (size_t i = 0; i < kParamCount; ++i) {
    const uint32_t id = paramSpec(i).id;
    if (preset::isSetupParam(id)) continue;
    INFO(id);
    REQUIRE(b.param(id) == a.param(id));
  }
  REQUIRE(b.patch().routes == a.patch().routes);
  REQUIRE(b.patch().shapes == a.patch().shapes);
  REQUIRE(b.engineStash() == a.engineStash());
}

TEST_CASE("Applying a Performance keeps the setup fields") {
  std::mt19937 rng(11);
  Synth src(PluginKind::Instrument);
  makeSetupNonDefault(src, 1);
  randomise(src, rng, [](uint32_t id) { return !preset::isSetupParam(id); });
  state::StateStore storeSrc;
  const std::string file = preset::save(src, storeSrc, meta(preset::Kind::Performance), -1);
  REQUIRE(nlohmann::json::parse(file)["state"]["params"].contains("16711680") == false);

  Synth dst(PluginKind::Instrument);
  makeSetupNonDefault(dst, 0);
  state::StateStore storeDst;
  REQUIRE(apply(dst, storeDst, file, 0) != state::LoadResult::Invalid);
  Synth expected(PluginKind::Instrument);
  makeSetupNonDefault(expected, 0);
  size_t setupSeen = 0;
  for (size_t i = 0; i < kParamCount; ++i) {
    const uint32_t id = paramSpec(i).id;
    if (preset::isSetupParam(id)) {
      ++setupSeen;
      INFO(id);
      REQUIRE(dst.param(id) == expected.param(id));
    } else {
      REQUIRE(dst.param(id) == src.param(id));
    }
  }
  REQUIRE(setupSeen == 2 * 8 + 3);
}

TEST_CASE("A Patch saved from A loads into B and leaves A alone") {
  std::mt19937 rng(23);
  Synth s(PluginKind::Instrument);
  makeSetupNonDefault(s, 0);
  s.setParam(partId(1, PartParam::KeyLow), 60);
  s.setParam(partId(1, PartParam::KeyHigh), 90);
  randomise(s, rng, [](uint32_t id) { return !preset::isPatchExcluded(id); });
  state::StateStore store;
  const std::string file = preset::save(s, store, meta(preset::Kind::Patch), 0);
  const auto parsed = nlohmann::json::parse(file);
  REQUIRE(parsed["kind"] == "patch");
  for (auto it = parsed["state"]["params"].begin(); it != parsed["state"]["params"].end(); ++it) {
    REQUIRE((std::stoul(it.key()) >> 16) == 0);
    REQUIRE_FALSE(preset::isPatchExcluded(uint32_t(std::stoul(it.key()))));
  }

  const Snap before(s);
  REQUIRE(apply(s, store, file, 1) != state::LoadResult::Invalid);

  for (size_t i = 0; i < kParamCount; ++i) {
    const uint32_t id = paramSpec(i).id;
    const uint32_t part = id >> 16;
    INFO(id);
    if (part == 0 || part == params::kMasterPart || preset::isPatchExcluded(id)) {
      // A, the master, and B's key range and setup fields are as they were.
      REQUIRE(s.param(id) == before.at(id));
    } else {
      // B's sound is A's old sound.
      REQUIRE(s.param(id) == before.at(id & 0xFFFF));
    }
  }
  REQUIRE(s.patch().routes[0] == before.patch.routes[0]);
  REQUIRE(s.patch().shapes[0] == before.patch.shapes[0]);
  REQUIRE(s.patch().routes[1] == before.patch.routes[0]);
  REQUIRE(s.patch().shapes[1] == before.patch.shapes[0]);
  REQUIRE(s.param(partId(1, PartParam::KeyLow)) == 60);
  REQUIRE(s.param(partId(1, PartParam::KeyHigh)) == 90);
}

TEST_CASE("A preset for the other plugin kind is refused") {
  Synth fx(PluginKind::Effect);
  state::StateStore storeFx;
  const std::string fxFile = preset::save(fx, storeFx, meta(preset::Kind::Performance, PluginKind::Effect), -1);
  Synth inst(PluginKind::Instrument);
  state::StateStore store;
  preset::ApplyResult r;
  REQUIRE(apply(inst, store, fxFile, 0, &r) == state::LoadResult::Invalid);
  REQUIRE(r.message.find("EG13 FX") != std::string::npos);

  const std::string instFile = preset::save(inst, store, meta(preset::Kind::Patch), 0);
  REQUIRE(apply(fx, storeFx, instFile, 0, &r) == state::LoadResult::Invalid);
  REQUIRE_FALSE(r.message.empty());
}

TEST_CASE("Malformed presets are refused, never thrown") {
  Synth s(PluginKind::Instrument);
  state::StateStore store;
  const std::string good = preset::save(s, store, meta(preset::Kind::Performance), -1);
  auto broken = nlohmann::json::parse(good);
  broken.erase("state");
  auto newer = nlohmann::json::parse(good);
  newer["eg13Preset"] = 2;
  auto badState = nlohmann::json::parse(good);
  badState["state"]["params"] = 5;
  auto badPatch = nlohmann::json::parse(good);
  badPatch["kind"] = "patch";
  badPatch["state"]["engineSettings"] = "x";
  badPatch["state"]["routes"] = nlohmann::json::array();

  const std::string cur = store.save(s);
  for (const std::string& text :
       {std::string(), good.substr(0, good.size() / 2), std::string("[]"), broken.dump(), newer.dump(),
        badState.dump(), std::string("{\"eg13Preset\":1,\"kind\":3}")}) {
    preset::ApplyResult r;
    std::string merged;
    REQUIRE_NOTHROW(merged = preset::mergeForApply(cur, text, PluginKind::Instrument, 0, r));
    REQUIRE(merged.empty());
    REQUIRE(r.result == state::LoadResult::Invalid);
    REQUIRE_FALSE(r.message.empty());
    REQUIRE_FALSE(preset::readMeta(text).has_value());
  }
  preset::ApplyResult r;
  preset::mergeForApply(cur, newer.dump(), PluginKind::Instrument, 0, r);
  REQUIRE(r.message.find("newer") != std::string::npos);
  // Odd section data inside a well-formed preset, a broken current state or a bad part: no throw.
  REQUIRE_NOTHROW(preset::mergeForApply(cur, badPatch.dump(), PluginKind::Instrument, 1, r));
  REQUIRE_NOTHROW(preset::mergeForApply("garbage", good, PluginKind::Instrument, 0, r));
  REQUIRE(r.result == state::LoadResult::Invalid);
  const std::string patch = preset::save(s, store, meta(preset::Kind::Patch), 0);
  REQUIRE(preset::mergeForApply(cur, patch, PluginKind::Instrument, 5, r).empty());
  REQUIRE(preset::save(s, store, meta(preset::Kind::Patch), 2).empty());
}

TEST_CASE("readMeta reads name, bank, kind and credit") {
  Synth s(PluginKind::Effect);
  state::StateStore store;
  preset::Meta m = meta(preset::Kind::Patch, PluginKind::Effect);
  m.name = "Glass Choir";
  m.bank = "Bank 2";
  m.note = "n";
  m.credit = "Westlee Latta";
  m.licence = "CC0";
  const auto read = preset::readMeta(preset::save(s, store, m, 1));
  REQUIRE(read.has_value());
  REQUIRE(read->kind == preset::Kind::Patch);
  REQUIRE(read->plugin == PluginKind::Effect);
  REQUIRE(read->name == "Glass Choir");
  REQUIRE(read->bank == "Bank 2");
  REQUIRE(read->note == "n");
  REQUIRE(read->credit == "Westlee Latta");
  REQUIRE(read->licence == "CC0");
}
