#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "preset/factory.h"
#include "preset/preset.h"
#include "preset_lib.h"
#include "state/state.h"
#include "mod/mod_routes.h"
#include "synth/param_table.h"
#include "synth/synth.h"
#include "util/soft_clip.h"

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

float peakOf(const Audio& audio) {
  float peak = 0.0f;
  for (size_t i = 0; i < audio.left.size(); ++i) peak = std::max({peak, std::abs(audio.left[i]), std::abs(audio.right[i])});
  return peak;
}

constexpr double kPi = 3.14159265358979323846;

std::string engineName(const Synth& synth, SlotKind kind, int part) {
  return slotEngineInfo(kind, synth.engine(kind, part)).displayName;
}

// The spectral centroid (Hz) of the mono mix of [from, from + n): Hann window, zero-padded FFT.
double spectralCentroid(const Audio& audio, size_t from, size_t n) {
  size_t size = 1;
  while (size < n) size <<= 1;
  std::vector<std::complex<double>> x(size);
  for (size_t i = 0; i < n; ++i) {
    const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * double(i) / double(n - 1));
    x[i] = w * 0.5 * (double(audio.left[from + i]) + double(audio.right[from + i]));
  }
  for (size_t i = 1, j = 0; i < size; ++i) {  // iterative radix-2 FFT
    size_t bit = size >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(x[i], x[j]);
  }
  for (size_t len = 2; len <= size; len <<= 1) {
    const std::complex<double> step = std::polar(1.0, -2.0 * kPi / double(len));
    for (size_t i = 0; i < size; i += len) {
      std::complex<double> w = 1.0;
      for (size_t k = 0; k < len / 2; ++k, w *= step) {
        const std::complex<double> a = x[i + k], b = x[i + k + len / 2] * w;
        x[i + k] = a + b;
        x[i + k + len / 2] = a - b;
      }
    }
  }
  double weighted = 0.0, total = 0.0;
  for (size_t k = 1; k < size / 2; ++k) {
    const double m = std::abs(x[k]);
    weighted += m * double(k) * kSampleRate / double(size);
    total += m;
  }
  return total > 0.0 ? weighted / total : 0.0;
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

TEST_CASE("Every Bank 2 preset carries the catnap credit and CC BY 4.0") {
  size_t count = 0;
  const preset::FactoryPreset* table = preset::factoryPresets(count);
  int bank2 = 0;
  for (size_t i = 0; i < count; ++i) {
    if (std::string(table[i].bank) != "Bank 2") continue;
    ++bank2;
    INFO(label(table[i]));
    REQUIRE(std::string(table[i].credit) == "~zero-g:catnap by plantay (patchstorage.com/zero-gcatnap-vcv-rack-jam)");
    REQUIRE(std::string(table[i].licence) == "CC BY 4.0");
    // The compiled preset itself carries them too, so a copy saved from it keeps the attribution.
    const auto meta = preset::readMeta(table[i].json);
    REQUIRE(meta);
    REQUIRE(meta->credit == table[i].credit);
    REQUIRE(meta->licence == table[i].licence);
  }
  REQUIRE(bank2 >= 4);  // the full rebuild and at least three variations
}

TEST_CASE("Exactly one instrument preset is marked firstLoad") {
  size_t count = 0;
  const preset::FactoryPreset* table = preset::factoryPresets(count);
  int marked = 0;
  for (size_t i = 0; i < count; ++i) {
    if (!table[i].firstLoad) continue;
    ++marked;
    INFO(label(table[i]));
    REQUIRE(table[i].plugin == PluginKind::Instrument);
  }
  REQUIRE(marked == 1);
}

TEST_CASE("The first-load patch is a two-part ring-mod and granular Performance that evolves") {
  size_t count = 0;
  const preset::FactoryPreset* table = preset::factoryPresets(count);
  const preset::FactoryPreset* first = nullptr;
  for (size_t i = 0; i < count; ++i) {
    if (table[i].firstLoad) first = &table[i];
  }
  REQUIRE(first != nullptr);
  INFO(label(*first));
  CHECK(std::string(first->bank) == "Bank 1");
  REQUIRE(first->kind == preset::Kind::Performance);
  REQUIRE(first->plugin == PluginKind::Instrument);

  // Structure: both parts play, one Shaper is the Ring Mod, one Part FX is granular, and each part
  // has a slow per-part modulator routed somewhere.
  Synth synth(PluginKind::Instrument);
  state::StateStore store;
  preset::ApplyResult applied;
  const std::string merged = preset::mergeForApply(store.save(synth), first->json, PluginKind::Instrument, 0, applied);
  REQUIRE(store.load(synth, merged) == state::LoadResult::Ok);
  CHECK(synth.param(paramSpec(masterParamIndex(MasterParam::Mode)).id) == double(PartMode::Layer));
  const PatchData patch = synth.patch();
  bool ringMod = false, granular = false;
  for (int part = 0; part < kNumParts; ++part) {
    INFO("part " << part);
    CHECK(synth.param(paramSpec(partParamIndex(part, PartParam::Level)).id) > 0.0);
    ringMod = ringMod || engineName(synth, SlotKind::Shaper, part) == "Ring Mod / Vocoder";
    const std::string fx = engineName(synth, SlotKind::PartFx, part);
    granular = granular || fx == "Granular" || fx == "Granular Delay";
    bool slowModulator = false;
    for (int slot = 0; slot < kNumModSlots; ++slot) {
      const std::string mod = engineName(synth, modSlotKind(slot), part);
      const bool perPart = synth.param(paramSpec(modScopeParamIndex(part, slot)).id) >= 0.5;
      if (!perPart || (mod != "LFO" && mod != "Random / S&H" && mod != "Function Generator")) continue;
      for (const Route& route : patch.routes[size_t(part)]) {
        const int from = int(route.source) - kSrcMod;
        if (routeActive(route) && from >= 0 && from / kMaxModOutputs == slot) slowModulator = true;
      }
    }
    CHECK(slowModulator);
  }
  CHECK(ringMod);
  CHECK(granular);

  // Sound: both parts are heard, nothing clips, and the colour moves under the held chord.
  Audio audio;
  std::string error;
  RenderOptions options;
  options.softClip = false;  // the raw mix: with Soft Clip on, the output can hardly reach full scale
  REQUIRE(render(first->json, 0, kDefaultSeconds, audio, error, &options));
  for (int part = 0; part < kNumParts; ++part) {
    INFO("part " << part << " peak " << 20.0 * std::log10(options.partPeaks[part] + 1e-12) << " dBFS");
    CHECK(options.partPeaks[part] > std::pow(10.0f, -40.0f / 20.0f));
  }
  const float peak = peakOf(audio);
  INFO("output peak " << 20.0 * std::log10(peak + 1e-12) << " dBFS");
  CHECK(peak < kSoftClipThreshold);  // the note script never reaches the soft clipper
  const size_t frame = size_t(kSampleRate / 2);
  std::vector<double> centroids;
  for (size_t from = 2 * size_t(kSampleRate); from + frame <= 8 * size_t(kSampleRate); from += frame) {
    centroids.push_back(spectralCentroid(audio, from, frame));
  }
  REQUIRE(centroids.size() == 12);
  double mean = 0.0, variance = 0.0;
  for (const double c : centroids) mean += c / double(centroids.size());
  for (const double c : centroids) variance += (c - mean) * (c - mean) / double(centroids.size());
  const double cv = std::sqrt(variance) / mean;
  INFO("centroid mean " << mean << " Hz, coefficient of variation " << cv);
  CHECK(cv >= 0.05);
}

TEST_CASE("Every instrument factory preset keeps 1 dB of headroom at eight notes, velocity 127") {
  // Played hard and wide (stressScript), with Soft Clip off, the raw mix stays below -1 dBFS.
  size_t count = 0;
  const preset::FactoryPreset* table = preset::factoryPresets(count);
  for (size_t i = 0; i < count; ++i) {
    if (table[i].plugin != PluginKind::Instrument) continue;
    INFO(label(table[i]));
    Audio audio;
    std::string error;
    RenderOptions options;
    options.softClip = false;
    REQUIRE(renderScript(table[i].json, 0, stressScript(), audio, error, &options));
    const float peak = peakOf(audio);
    INFO("peak " << 20.0 * std::log10(peak + 1e-12) << " dBFS");
    CHECK(peak < std::pow(10.0f, -1.0f / 20.0f));
  }
}

TEST_CASE("Every instrument factory preset keeps 1 dB of headroom and no DC at the expression extremes") {
  // Ruling R6: eight notes up to C7 at velocity 127 with the mod wheel, slide, channel and poly
  // pressure at their maximum (extremesScript), Soft Clip off: the raw mix stays below -1 dBFS and
  // each channel's mean below -50 dBFS.
  size_t count = 0;
  const preset::FactoryPreset* table = preset::factoryPresets(count);
  for (size_t i = 0; i < count; ++i) {
    if (table[i].plugin != PluginKind::Instrument) continue;
    INFO(label(table[i]));
    Audio audio;
    std::string error;
    RenderOptions options;
    options.softClip = false;
    REQUIRE(renderScript(table[i].json, 0, extremesScript(), audio, error, &options));
    const float peak = peakOf(audio);
    double left = 0.0, right = 0.0;
    for (size_t k = 0; k < audio.left.size(); ++k) {
      left += double(audio.left[k]);
      right += double(audio.right[k]);
    }
    const double dc = std::max(std::fabs(left), std::fabs(right)) / double(audio.left.size());
    INFO("peak " << 20.0 * std::log10(peak + 1e-12) << " dBFS, DC " << 20.0 * std::log10(dc + 1e-12) << " dBFS");
    CHECK(peak < std::pow(10.0f, -1.0f / 20.0f));
    CHECK(dc < std::pow(10.0, -50.0 / 20.0));
  }
}

TEST_CASE("A factory render does not depend on what rendered before it") {
  // Braids, Plaits and the other modules draw from one process-wide generator: renderScript
  // starts it from its start value, so a preset sounds the same alone, after others, in the CLI
  // and in every ctest, whatever the table order.
  size_t count = 0;
  const preset::FactoryPreset* table = preset::factoryPresets(count);
  std::vector<size_t> instruments;
  for (size_t i = 0; i < count; ++i) {
    if (table[i].plugin == PluginKind::Instrument) instruments.push_back(i);
  }
  REQUIRE(instruments.size() >= 3);
  std::string error;
  for (const size_t pick : {instruments[1], instruments.back()}) {
    INFO(label(table[pick]));
    Audio alone, after;
    REQUIRE(renderScript(table[pick].json, 0, stressScript(), alone, error));
    for (const size_t other : instruments) {
      if (other == pick) continue;
      Audio scratch;
      REQUIRE(renderScript(table[other].json, 0, stressScript(), scratch, error));
    }
    REQUIRE(renderScript(table[pick].json, 0, stressScript(), after, error));
    REQUIRE(alone.left == after.left);
    REQUIRE(alone.right == after.right);
  }
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
  // Restores the generated table when the test ends, also if a REQUIRE fails.
  struct Restore {
    ~Restore() { preset::setFactoryPresetsForTesting(nullptr, 0); }
  };
  size_t count = 0;
  {
    Restore restore;
    preset::setFactoryPresetsForTesting(&one, 1);
    REQUIRE(preset::factoryPresets(count) == &one);
    REQUIRE(count == 1);
  }
  preset::factoryPresets(count);
  REQUIRE(count == generated);
}

TEST_CASE("presetInfo lists the fields the listening script needs, for a source and a preset file") {
  std::string lines, error;
  REQUIRE(presetInfo(fixture("test-performance.json"), lines, error));
  CHECK(lines.find("name=Test Performance\n") != std::string::npos);
  CHECK(lines.find("kind=performance\n") != std::string::npos);
  CHECK(lines.find("plugin=instrument\n") != std::string::npos);
  CHECK(lines.find("order=10\n") != std::string::npos);
  CHECK(lines.find("note=") != std::string::npos);
  CHECK(lines.find("credit=EG13 tests\n") != std::string::npos);

  const Compiled compiled = compileFixture("test-patch.json");
  REQUIRE(presetInfo(compiled.json, lines, error));
  CHECK(lines.find("name=Test Patch\n") != std::string::npos);
  CHECK(lines.find("kind=patch\n") != std::string::npos);
  CHECK(lines.find("order=0\n") != std::string::npos);  // a preset file has no order

  CHECK_FALSE(presetInfo("[1]", lines, error));
}
