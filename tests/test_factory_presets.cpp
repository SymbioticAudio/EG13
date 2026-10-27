#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include "preset/factory.h"
#include "preset/preset.h"
#include "preset_lib.h"
#include "state/state.h"
#include "synth/synth.h"

using namespace eg13;
using namespace eg13::presettool;

namespace {

std::string slurp(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  REQUIRE(in);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

std::string fixture(const char* name) { return slurp(std::string(EG13_PRESETS_DIR) + "/test/" + name); }

std::string label(const preset::FactoryPreset& p) { return std::string(p.bank) + "/" + p.name; }

Compiled compileFixture(const char* name) {
  Compiled compiled;
  std::string error;
  INFO(error);
  REQUIRE(compileSource(fixture(name), name, "", compiled, error));
  return compiled;
}

}  // namespace

TEST_CASE("Every factory preset compiles and applies cleanly") {
  size_t count = 0;
  const preset::FactoryPreset* table = preset::factoryPresets(count);
  for (size_t i = 0; i < count; ++i) {
    INFO(label(table[i]));
    const std::string json = table[i].json;
    const auto meta = preset::readMeta(json);
    REQUIRE(meta);
    REQUIRE(meta->kind == table[i].kind);
    REQUIRE(meta->plugin == table[i].plugin);
    Synth synth(table[i].plugin);
    const state::StateStore store;
    preset::ApplyResult result;
    const std::string merged = preset::mergeForApply(store.save(synth), json, table[i].plugin, 0, result);
    INFO(result.message);
    REQUIRE(result.result == state::LoadResult::Ok);
    REQUIRE_FALSE(merged.empty());
  }
}

TEST_CASE("Factory preset names are unique within a bank") {
  size_t count = 0;
  const preset::FactoryPreset* table = preset::factoryPresets(count);
  std::set<std::string> seen;
  for (size_t i = 0; i < count; ++i) {
    INFO(label(table[i]));
    REQUIRE(seen.insert(label(table[i])).second);
  }
}

TEST_CASE("Exactly one instrument preset is marked firstLoad") {
  size_t count = 0;
  const preset::FactoryPreset* table = preset::factoryPresets(count);
  if (count == 0) SKIP("the factory table is empty until Task 7 (Bank 1) adds presets");
  int marked = 0;
  for (size_t i = 0; i < count; ++i) {
    if (!table[i].firstLoad) continue;
    ++marked;
    INFO(label(table[i]));
    REQUIRE(table[i].plugin == PluginKind::Instrument);
  }
  REQUIRE(marked == 1);
}

TEST_CASE("Each factory preset matches its golden level envelope") {
  const nlohmann::json golden = nlohmann::json::parse(slurp(std::string(EG13_TEST_DATA_DIR) + "/preset_golden.json"));
  REQUIRE(golden.contains("presets"));
  size_t count = 0;
  const preset::FactoryPreset* table = preset::factoryPresets(count);
  REQUIRE(golden["presets"].size() == count);  // a preset added or removed needs `eg13_preset golden --write`
  for (size_t i = 0; i < count; ++i) {
    INFO(label(table[i]));
    REQUIRE(golden["presets"].contains(label(table[i])));
    Audio audio;
    std::string error;
    REQUIRE(render(table[i].json, 0, kDefaultSeconds, audio, error));
    const std::vector<double> expected = golden["presets"][label(table[i])]["envelope"].get<std::vector<double>>();
    std::string why;
    INFO(why);
    REQUIRE(envelopesMatch(expected, levelEnvelope(audio), why));
  }
}

TEST_CASE("The golden comparison ignores quiet frames and allows 1.5 dB") {
  std::string why;
  REQUIRE(envelopesMatch({-20, -30, -90}, {-21.4, -28.6, -70}, why));  // the third is below the floor in both
  REQUIRE_FALSE(envelopesMatch({-20, -30}, {-21.6, -30}, why));
  REQUIRE_FALSE(envelopesMatch({-20, -30}, {-20, -30, -30}, why));  // length differs
  REQUIRE_FALSE(envelopesMatch({-20, -90}, {-20, -40}, why));       // sound where the golden has none
}

TEST_CASE("compile rejects an unknown engine or parameter name, naming the preset and the name") {
  Compiled compiled;
  std::string error;
  REQUIRE_FALSE(compileSource(fixture("bad-engine.json"), "presets/test/bad-engine.json", "", compiled, error));
  CHECK(error.find("presets/test/bad-engine.json") != std::string::npos);
  CHECK(error.find("Warp Drive") != std::string::npos);

  std::string source = fixture("test-performance.json");
  const size_t at = source.find("\"Harmonics\"");
  REQUIRE(at != std::string::npos);
  source.replace(at, 11, "\"Harmonix\"");
  error.clear();
  REQUIRE_FALSE(compileSource(source, "presets/test/typo.json", "", compiled, error));
  CHECK(error.find("presets/test/typo.json") != std::string::npos);
  CHECK(error.find("Harmonix") != std::string::npos);

  source = fixture("test-performance.json");
  const size_t opt = source.find("\"Layer\"");
  REQUIRE(opt != std::string::npos);
  source.replace(opt, 7, "\"Lair\"");
  error.clear();
  REQUIRE_FALSE(compileSource(source, "presets/test/option.json", "", compiled, error));
  CHECK(error.find("Lair") != std::string::npos);

  source = fixture("test-performance.json");
  const size_t route = source.find("\"Velocity\"");
  REQUIRE(route != std::string::npos);
  source.replace(route, 10, "\"Velocitee\"");
  error.clear();
  REQUIRE_FALSE(compileSource(source, "presets/test/route.json", "", compiled, error));
  CHECK(error.find("Velocitee") != std::string::npos);
}

TEST_CASE("compile writes a complete preset the core reads back") {
  const Compiled perf = compileFixture("test-performance.json");
  const auto meta = preset::readMeta(perf.json);
  REQUIRE(meta);
  CHECK(meta->kind == preset::Kind::Performance);
  CHECK(meta->plugin == PluginKind::Instrument);
  CHECK(meta->name == "Test Performance");
  CHECK(perf.order == 10);
  Synth synth(PluginKind::Instrument);
  state::StateStore store;
  preset::ApplyResult result;
  const std::string merged = preset::mergeForApply(store.save(synth), perf.json, PluginKind::Instrument, 0, result);
  REQUIRE(result.result == state::LoadResult::Ok);
  REQUIRE(store.load(synth, merged) == state::LoadResult::Ok);
  CHECK(synth.engine(SlotKind::Source, 0) == 1);  // Digital Oscillator
  CHECK(synth.engine(SlotKind::Source, 1) == 0);

  const Compiled patch = compileFixture("test-patch.json");
  const auto patchMeta = preset::readMeta(patch.json);
  REQUIRE(patchMeta);
  CHECK(patchMeta->kind == preset::Kind::Patch);
}

TEST_CASE("decompile then compile reproduces the preset") {
  for (const char* name : {"test-performance.json", "test-patch.json"}) {
    INFO(name);
    const Compiled first = compileFixture(name);
    std::string source, error;
    REQUIRE(decompile(first.json, source, error));
    Compiled second;
    INFO(error);
    REQUIRE(compileSource(source, "decompiled", "", second, error));
    REQUIRE(second.json == first.json);
  }
}

TEST_CASE("render plays the note script and a Patch can go to part B") {
  Audio audio;
  std::string error;
  const Compiled patch = compileFixture("test-patch.json");
  REQUIRE(render(patch.json, 0, 3.0, audio, error));
  REQUIRE(audio.left.size() == 144000);
  const std::vector<double> env = levelEnvelope(audio);
  REQUIRE(env.size() == 12);
  CHECK(env[4] > -60.0);
  Audio b;
  REQUIRE(render(patch.json, 1, 3.0, b, error));
  CHECK(levelEnvelope(b)[4] > -60.0);
}

TEST_CASE("Engine parameter names are unique within an engine, apart from (unused)") {
  for (int k = 0; k < kNumSlotKinds; ++k) {
    for (int e = 0; e < slotEngineCount(SlotKind(k)); ++e) {
      const EngineInfo& info = slotEngineInfo(SlotKind(k), e);
      INFO(info.displayName);
      std::set<std::string> names;
      for (int p = 0; p < info.paramCount; ++p) {
        if (std::string(info.params[p].name) == "(unused)") continue;
        REQUIRE(names.insert(info.params[p].name).second);
      }
    }
  }
}

TEST_CASE("setFactoryPresetsForTesting swaps the table and nullptr restores it") {
  size_t generated = 0;
  preset::factoryPresets(generated);
  const Compiled compiled = compileFixture("test-patch.json");
  const std::string json = compiled.json;
  const preset::FactoryPreset one = {"Bank 1", "Only", "", "", "", preset::Kind::Patch, PluginKind::Instrument,
                                     false, 1, json.c_str()};
  preset::setFactoryPresetsForTesting(&one, 1);
  size_t count = 0;
  REQUIRE(preset::factoryPresets(count) == &one);
  REQUIRE(count == 1);
  preset::setFactoryPresetsForTesting(nullptr, 0);
  preset::factoryPresets(count);
  REQUIRE(count == generated);
}
