#include "preset_lib.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>

#include <nlohmann/json.hpp>

#include "common/engine_names.h"
#include "core_api.h"
#include "mod/mod_routes.h"
#include "params/param_ids.h"
#include "state/state.h"
#include "synth/param_table.h"
#include "synth/synth.h"
#include "ui_bridge.h"

namespace eg13::presettool {
namespace {

using json = nlohmann::json;
using ojson = nlohmann::ordered_json;
namespace fs = std::filesystem;

// Raised inside the compiler and decompiler, caught at their entry points.
struct Fail : std::runtime_error {
  using std::runtime_error::runtime_error;
};

[[noreturn]] void fail(const std::string& file, const std::string& where, const std::string& what) {
  throw Fail(file + ": " + (where.empty() ? "" : where + ": ") + what);
}

struct SlotName {
  const char* key;
  SlotKind kind;
};
const SlotName kSlots[] = {{"Source", SlotKind::Source}, {"Filter", SlotKind::Filter},
                           {"Shaper", SlotKind::Shaper}, {"Part FX", SlotKind::PartFx},
                           {"Mod 1", SlotKind::Mod1},    {"Mod 2", SlotKind::Mod2},
                           {"Mod 3", SlotKind::Mod3},    {"Mod 4", SlotKind::Mod4}};
const char* const kPartKeys[kNumParts] = {"A", "B"};

// The route names the editor shows, for one part of one synth, through the C API's own functions
// so the tool can never name things differently from the editor. Built after the slots are set:
// mod outputs and destination labels follow the part's engines.
class RouteNames {
 public:
  explicit RouteNames(Synth& synth)
      : bridge_(synth, {}), context_{synth, bridge_, {}, {}, {}, {}, {}, {}}, api_(makeCoreApi(context_)) {}

  std::string source(int part, uint16_t code) const { return name(part, code, true); }
  std::string destination(int part, uint16_t code) const { return name(part, code, false); }

  // Code of the named source or destination, or -1.
  int find(int part, const std::string& wanted, bool isSource) const {
    const uint16_t count = isSource ? uint16_t(kNumSources) : uint16_t(kNumDestinations);
    for (uint16_t code = 1; code < count; ++code) {
      const std::string n = name(part, code, isSource);
      if (!n.empty() && tools::sameName(wanted, n.c_str())) return code;
    }
    return -1;
  }

 private:
  std::string name(int part, uint16_t code, bool isSource) const {
    char buffer[EG13_TEXT_SIZE] = {};
    const bool ok = isSource ? api_.source_name(api_.context, uint32_t(part), code, buffer, sizeof buffer)
                             : api_.destination_name(api_.context, uint32_t(part), code, buffer, sizeof buffer);
    return ok ? buffer : std::string();
  }
  UiBridge bridge_;
  CoreApiContext context_;
  EG13CoreAPI api_;
};

std::string text(const json& j, const char* key) {
  const auto it = j.find(key);
  return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

void onlyKeys(const json& object, std::initializer_list<const char*> allowed, const std::string& file,
              const std::string& where) {
  for (auto it = object.begin(); it != object.end(); ++it) {
    bool known = false;
    for (const char* key : allowed) known = known || it.key() == key;
    if (!known) fail(file, where, "unknown key \"" + it.key() + "\"");
  }
}

// string_view parameters: a const std::string& here would let callers pass temporaries to a function
// that returns a reference (GCC 13 -Wdangling-reference).
const json& object(const json& parent, const char* key, std::string_view file, std::string_view where) {
  const json& value = parent.at(key);
  if (!value.is_object()) fail(std::string(file), std::string(where), std::string("\"") + key + "\" must be an object");
  return value;
}

// ---- compile -----------------------------------------------------------------------------

// A slot P's value: a number 0..1, or the option it shows.
double engineValue(const json& value, const EngineParamInfo& param, const std::string& file,
                   const std::string& where, const std::string& name) {
  if (value.is_number()) {
    const double v = value.get<double>();
    if (!(v >= 0.0 && v <= 1.0)) fail(file, where, "\"" + name + "\" must be 0..1, not " + value.dump());
    return v;
  }
  if (value.is_string()) {
    float v = 0.0f;
    if (!tools::valueForOption(param, value.get<std::string>(), &v)) {
      fail(file, where, "unknown option " + value.dump() + " for \"" + name + "\"");
    }
    return double(v);
  }
  fail(file, where, "\"" + name + "\" must be a number or an option name");
}

// A part or master setting, in its own units; stepped ones also by option name.
double settingValue(const json& value, size_t index, const std::string& file, const std::string& where,
                    const std::string& name) {
  const ParamSpec& spec = paramSpec(index);
  if (value.is_number()) {
    const double v = value.get<double>();
    if (!(v >= double(spec.minValue) && v <= double(spec.maxValue))) {
      fail(file, where, "\"" + name + "\" must be " + std::to_string(int(spec.minValue)) + ".." +
                            std::to_string(int(spec.maxValue)) + " (or a fraction for a continuous setting), not " +
                            value.dump());
    }
    return v;
  }
  if (value.is_string() && spec.stepped) {
    for (int v = int(spec.minValue); v <= int(spec.maxValue); ++v) {
      char buffer[64] = {};
      formatParam(index, double(v), buffer, sizeof buffer);
      if (tools::sameName(value.get<std::string>(), buffer)) return double(v);
    }
    fail(file, where, "unknown option " + value.dump() + " for \"" + name + "\"");
  }
  fail(file, where, "\"" + name + "\" must be a number" + std::string(spec.stepped ? " or an option name" : ""));
}

void applySettings(Synth& synth, const json& settings, int part, const std::string& file, const std::string& where) {
  for (auto it = settings.begin(); it != settings.end(); ++it) {
    bool found = false;
    for (size_t i = 0; i < kPartParamCount && !found; ++i) {
      const size_t index = partParamIndex(part, PartParam(i + 1));
      if (!tools::sameName(it.key(), paramSpec(index).name)) continue;
      found = true;
      synth.setParam(paramSpec(index).id, settingValue(it.value(), index, file, where, it.key()));
    }
    if (!found) fail(file, where, "unknown setting \"" + it.key() + "\"");
  }
}

void applySlot(Synth& synth, const json& slot, SlotKind kind, int part, const std::string& file,
               const std::string& where) {
  if (!slot.is_object()) fail(file, where, "must be an object");
  onlyKeys(slot, {"engine", "scope", "params"}, file, where);
  int engine = synth.engine(kind, part);
  if (slot.contains("engine")) {
    if (!slot["engine"].is_string()) fail(file, where, "\"engine\" must be a name");
    engine = tools::engineByName(kind, slot["engine"].get<std::string>());
    if (engine < 0) fail(file, where, "unknown engine " + slot["engine"].dump());
    synth.setParam(paramSpec(slotEngineParamIndex(kind, part)).id, double(engine));
  }
  const EngineInfo& info = slotEngineInfo(kind, engine);
  if (slot.contains("params")) {
    const json& params = object(slot, "params", file, where);
    for (auto it = params.begin(); it != params.end(); ++it) {
      const int p = tools::paramByName(info, it.key());
      if (p < 0) fail(file, where, "engine \"" + std::string(info.displayName) + "\" has no parameter \"" + it.key() + "\"");
      synth.setParam(paramSpec(slotParamIndex(kind, part, p)).id,
                     engineValue(it.value(), info.params[p], file, where + " " + info.displayName, it.key()));
    }
  }
  if (slot.contains("scope")) {
    if (!isModSlot(kind)) fail(file, where, "only a mod slot has a scope");
    const size_t index = modScopeParamIndex(part, modSlotIndex(kind));
    synth.setParam(paramSpec(index).id, settingValue(slot["scope"], index, file, where, "scope"));
  }
}

void applyRoutes(Synth& synth, const json& routes, int part, const std::string& file, const std::string& where) {
  if (!routes.is_array()) fail(file, where, "\"routes\" must be a list");
  for (int r = 0; r < kNumRoutes; ++r) {
    synth.setRoute(part, r, Route{});
    synth.setParam(paramSpec(routeAmountParamIndex(part, r)).id, 0.0);
  }
  const RouteNames names(synth);
  int next = 0;
  for (const json& entry : routes) {
    if (!entry.is_object()) fail(file, where, "a route must be an object");
    onlyKeys(entry, {"source", "via", "destination", "amount", "slot"}, file, where + " route");
    for (const char* required : {"source", "destination", "amount"}) {
      if (!entry.contains(required)) fail(file, where, std::string("a route needs \"") + required + "\"");
    }
    int slot = next;
    if (entry.contains("slot")) {
      if (!entry["slot"].is_number_integer()) fail(file, where, "\"slot\" must be 1..12");
      slot = entry["slot"].get<int>() - 1;
    }
    if (slot < 0 || slot >= kNumRoutes) fail(file, where, "more routes than the 12 slots, or a bad \"slot\"");
    next = slot + 1;
    Route route;
    auto resolve = [&](const char* key, bool isSource, bool optional) -> uint16_t {
      if (!entry.contains(key)) return 0;
      const std::string wanted = entry[key].is_string() ? entry[key].get<std::string>() : std::string();
      if (optional && wanted.empty()) return 0;
      const int code = wanted.empty() ? -1 : names.find(part, wanted, isSource);
      if (code < 0) fail(file, where, std::string("unknown route ") + key + " " + entry[key].dump());
      return uint16_t(code);
    };
    route.source = resolve("source", true, false);
    route.via = resolve("via", true, true);
    route.destination = resolve("destination", false, false);
    if (!entry["amount"].is_number()) fail(file, where, "a route's \"amount\" must be a number, -1..1");
    const double amount = entry["amount"].get<double>();
    if (!(amount >= -1.0 && amount <= 1.0)) fail(file, where, "a route's \"amount\" must be -1..1");
    synth.setRoute(part, slot, route);
    synth.setParam(paramSpec(routeAmountParamIndex(part, slot)).id, amount);
  }
}

// ---- render ------------------------------------------------------------------------------

constexpr int kNotes[4] = {48, 55, 62, 69};  // C3 G3 D4 A4
constexpr double kHoldSeconds = 8.0;
constexpr float kVelocity = 100.0f / 127.0f;

double dbOf(double meanSquare) { return meanSquare > 1e-12 ? 10.0 * std::log10(meanSquare) : -120.0; }

std::string slurp(const std::string& path, std::string& error) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    error = "cannot read " + path;
    return {};
  }
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

std::string bankName(const std::string& dir) {
  std::string base = fs::path(dir).filename().string();
  if (base.empty()) base = fs::path(dir).parent_path().filename().string();
  if (base == "bank1") return "Bank 1";
  if (base == "bank2-catnap") return "Bank 2";
  if (base == "fx") return "FX";
  return base;
}

std::string cString(const std::string& s) {
  std::string out = "\"";
  for (const char c : s) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (c == '"') out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if (u >= 0x20 && u < 0x7f && c != '?') out += c;  // '?' avoids trigraphs
    else {
      char buf[8];
      std::snprintf(buf, sizeof buf, "\\%03o", unsigned(u));
      out += buf;
    }
  }
  return out + "\"";
}

// A long literal as adjacent pieces, so no compiler meets a huge single token.
std::string longString(const std::string& s) {
  constexpr size_t kPiece = 2000;
  if (s.size() <= kPiece) return cString(s);
  std::string out;
  for (size_t at = 0; at < s.size(); at += kPiece) out += (at ? "\n      " : "") + cString(s.substr(at, kPiece));
  return out;
}

}  // namespace

bool compileSource(const std::string& textIn, const std::string& file, const std::string& bank, Compiled& out,
                   std::string& error) {
  try {
    json root = json::parse(textIn, nullptr, false);
    if (!root.is_object()) fail(file, "", "not a JSON object");
    onlyKeys(root, {"kind", "plugin", "name", "note", "credit", "licence", "order", "firstLoad", "master", "parts"},
             file, "");
    const std::string kind = text(root, "kind");
    const std::string plugin = text(root, "plugin");
    if (kind != "performance" && kind != "patch") fail(file, "", "\"kind\" must be \"performance\" or \"patch\"");
    if (plugin != "instrument" && plugin != "effect") fail(file, "", "\"plugin\" must be \"instrument\" or \"effect\"");
    const bool isPatch = kind == "patch";
    Compiled result;
    result.meta.kind = isPatch ? preset::Kind::Patch : preset::Kind::Performance;
    result.meta.plugin = plugin == "effect" ? PluginKind::Effect : PluginKind::Instrument;
    result.meta.name = text(root, "name");
    result.meta.bank = bank;
    result.meta.note = text(root, "note");
    result.meta.credit = text(root, "credit");
    result.meta.licence = text(root, "licence");
    if (result.meta.name.empty()) fail(file, "", "\"name\" is missing");
    if (root.contains("order")) {
      if (!root["order"].is_number_integer()) fail(file, "", "\"order\" must be an integer");
      result.order = root["order"].get<int>();
    }
    if (root.contains("firstLoad")) {
      if (!root["firstLoad"].is_boolean()) fail(file, "", "\"firstLoad\" must be true or false");
      result.firstLoad = root["firstLoad"].get<bool>();
    }

    Synth synth(result.meta.plugin);
    if (root.contains("master")) {
      if (isPatch) fail(file, "master", "a patch has no master settings");
      const json& master = object(root, "master", file, "master");
      for (auto it = master.begin(); it != master.end(); ++it) {
        bool found = false;
        for (size_t i = 0; i < kMasterParamCount && !found; ++i) {
          const size_t index = masterParamIndex(MasterParam(i));
          if (!tools::sameName(it.key(), paramSpec(index).name)) continue;
          found = true;
          synth.setParam(paramSpec(index).id, settingValue(it.value(), index, file, "master", it.key()));
        }
        if (!found) fail(file, "master", "unknown setting \"" + it.key() + "\"");
      }
    }
    if (root.contains("parts")) {
      const json& parts = object(root, "parts", file, "parts");
      for (auto it = parts.begin(); it != parts.end(); ++it) {
        int part = -1;
        for (int p = 0; p < kNumParts; ++p) {
          if (it.key() == kPartKeys[p]) part = p;
        }
        if (part < 0 || (isPatch && part != 0)) {
          fail(file, "parts", "unknown part \"" + it.key() + "\"" + (isPatch ? " (a patch has only part A)" : ""));
        }
        const std::string where = "part " + it.key();
        if (!it.value().is_object()) fail(file, where, "must be an object");
        onlyKeys(it.value(), {"Source", "Filter", "Shaper", "Part FX", "Mod 1", "Mod 2", "Mod 3", "Mod 4", "settings",
                              "routes"},
                 file, where);
        for (const SlotName& slot : kSlots) {
          if (it.value().contains(slot.key)) {
            applySlot(synth, it.value()[slot.key], slot.kind, part, file, where + " " + slot.key);
          }
        }
        if (it.value().contains("settings")) {
          applySettings(synth, object(it.value(), "settings", file, where), part, file, where + " settings");
        }
        if (it.value().contains("routes")) applyRoutes(synth, it.value()["routes"], part, file, where);
      }
    }
    result.json = preset::save(synth, state::StateStore{}, result.meta, isPatch ? 0 : -1);
    if (result.json.empty()) fail(file, "", "the core refused to save this preset");
    out = std::move(result);
    return true;
  } catch (const Fail& f) {
    error = f.what();
  } catch (const std::exception& e) {
    error = file + ": " + e.what();
  }
  return false;
}

// ---- decompile ---------------------------------------------------------------------------

namespace {

// The shortest decimal that reads back as the same float (JSON numbers are doubles here, so a bare
// float would print as 0.30000001192092896).
double shortFloat(double value) {
  const float f = float(value);
  char buffer[32];
  for (int digits = 1; digits <= 9; ++digits) {
    std::snprintf(buffer, sizeof buffer, "%.*g", digits, double(f));
    if (std::strtof(buffer, nullptr) == f) break;
  }
  return std::strtod(buffer, nullptr);
}

// A setting as the source writes it: the option name when it has one, else the number.
ojson settingJson(size_t index, double value) {
  const ParamSpec& spec = paramSpec(index);
  if (spec.stepped) {
    char buffer[64] = {};
    formatParam(index, value, buffer, sizeof buffer);
    // Only a name that maps back to exactly this value stands for it.
    if (buffer[0] != '\0') {
      for (int v = int(spec.minValue); v <= int(spec.maxValue); ++v) {
        char other[64] = {};
        formatParam(index, double(v), other, sizeof other);
        if (v != int(value) && std::string(other) == buffer) return ojson(int(value));
      }
      return ojson(std::string(buffer));
    }
    return ojson(int(value));
  }
  return ojson(shortFloat(value));
}

}  // namespace

bool decompile(const std::string& presetJson, std::string& sourceText, std::string& error) {
  try {
    const auto meta = preset::readMeta(presetJson);
    if (!meta) fail("preset", "", "not a preset this version can read");
    Synth synth(meta->plugin);
    state::StateStore store;
    preset::ApplyResult applied;
    const std::string merged = preset::mergeForApply(store.save(synth), presetJson, meta->plugin, 0, applied);
    if (merged.empty() || store.load(synth, merged) == state::LoadResult::Invalid) {
      fail("preset", "", applied.message.empty() ? "cannot apply" : applied.message);
    }
    const bool isPatch = meta->kind == preset::Kind::Patch;

    ojson root = ojson::object();
    root["kind"] = isPatch ? "patch" : "performance";
    root["plugin"] = meta->plugin == PluginKind::Effect ? "effect" : "instrument";
    root["name"] = meta->name;
    root["note"] = meta->note;
    root["credit"] = meta->credit;
    root["licence"] = meta->licence;
    root["order"] = 0;
    if (!isPatch) {
      ojson master = ojson::object();
      for (size_t i = 0; i < kMasterParamCount; ++i) {
        const size_t index = masterParamIndex(MasterParam(i));
        master[paramSpec(index).name] = settingJson(index, synth.param(paramSpec(index).id));
      }
      root["master"] = master;
    }
    const RouteNames names(synth);
    ojson parts = ojson::object();
    for (int part = 0; part < (isPatch ? 1 : kNumParts); ++part) {
      ojson out = ojson::object();
      for (const SlotName& slot : kSlots) {
        const int engine = synth.engine(slot.kind, part);
        const EngineInfo& info = slotEngineInfo(slot.kind, engine);
        ojson s = ojson::object();
        s["engine"] = info.displayName;
        if (isModSlot(slot.kind)) {
          const size_t index = modScopeParamIndex(part, modSlotIndex(slot.kind));
          s["scope"] = settingJson(index, synth.param(paramSpec(index).id));
        }
        if (info.paramCount > 0) {
          ojson params = ojson::object();
          for (int p = 0; p < info.paramCount; ++p) {
            if (tools::isUnusedParam(info.params[p])) continue;
            params[info.params[p].name] = shortFloat(synth.param(paramSpec(slotParamIndex(slot.kind, part, p)).id));
          }
          s["params"] = params;
        }
        out[slot.key] = s;
      }
      ojson settings = ojson::object();
      for (size_t i = 0; i < kPartParamCount; ++i) {
        const size_t index = partParamIndex(part, PartParam(i + 1));
        settings[paramSpec(index).name] = settingJson(index, synth.param(paramSpec(index).id));
      }
      out["settings"] = settings;
      ojson routes = ojson::array();
      int next = 0;
      for (int r = 0; r < kNumRoutes; ++r) {
        const Route route = synth.patch().routes[size_t(part)][size_t(r)];
        if (route.source == kSrcNone && route.destination == kDestNone) continue;
        ojson entry = ojson::object();
        if (r != next) entry["slot"] = r + 1;
        const std::string source = names.source(part, route.source);
        const std::string destination = names.destination(part, route.destination);
        const std::string via = route.via == kSrcNone ? std::string() : names.source(part, route.via);
        if (source.empty() || destination.empty() || (route.via != kSrcNone && via.empty())) {
          fail("preset", "part " + std::string(kPartKeys[part]), "route " + std::to_string(r + 1) + " uses a code this tool cannot name");
        }
        entry["source"] = source;
        if (!via.empty()) entry["via"] = via;
        entry["destination"] = destination;
        entry["amount"] = shortFloat(synth.param(paramSpec(routeAmountParamIndex(part, r)).id));
        routes.push_back(entry);
        next = r + 1;
      }
      out["routes"] = routes;
      parts[kPartKeys[part]] = out;
    }
    root["parts"] = parts;
    sourceText = root.dump(2) + "\n";
    return true;
  } catch (const Fail& f) {
    error = f.what();
  } catch (const std::exception& e) {
    error = e.what();
  }
  return false;
}

bool render(const std::string& presetJson, int part, double seconds, Audio& out, std::string& error) {
  const auto meta = preset::readMeta(presetJson);
  if (!meta) {
    error = "not a preset this version can read";
    return false;
  }
  if (part < 0 || part >= kNumParts) {
    error = "part must be A or B";
    return false;
  }
  Synth synth(meta->plugin);
  state::StateStore store;
  preset::ApplyResult applied;
  const std::string merged = preset::mergeForApply(store.save(synth), presetJson, meta->plugin, part, applied);
  if (merged.empty() || store.load(synth, merged) == state::LoadResult::Invalid) {
    error = applied.message.empty() ? "cannot apply the preset" : applied.message;
    return false;
  }
  if (meta->kind == preset::Kind::Patch && part == 1) {
    // Both parts hear every note, and the Init sound in A is muted, so only the patch is heard.
    synth.setParam(paramSpec(masterParamIndex(MasterParam::Mode)).id, double(PartMode::Layer));
    synth.setParam(paramSpec(partParamIndex(0, PartParam::Level)).id, 0.0);
  }
  synth.prepare(kSampleRate, kBlock);

  const size_t total = size_t(seconds * kSampleRate);
  const size_t hold = size_t(kHoldSeconds * kSampleRate);
  out.left.assign(total, 0.0f);
  out.right.assign(total, 0.0f);
  // An effect has nothing to process without an input: a 110 Hz saw and a little noise (a fixed
  // generator, the same on every machine) for as long as the notes are held.
  std::vector<float> inL, inR;
  const bool effect = meta->plugin == PluginKind::Effect;
  if (effect) {
    inL.assign(total, 0.0f);
    inR.assign(total, 0.0f);
    uint32_t state = 12345u;
    auto noise = [&] {
      state = state * 1664525u + 1013904223u;
      return float(state >> 8) / float(1u << 24) - 0.5f;
    };
    for (size_t i = 0; i < std::min(total, hold); ++i) {
      const float saw = 2.0f * float(std::fmod(double(i) * 110.0 / kSampleRate, 1.0)) - 1.0f;
      inL[i] = 0.25f * saw + 0.1f * noise();
      inR[i] = 0.25f * saw + 0.1f * noise();
    }
  }
  for (const int note : kNotes) synth.noteOn(0, note, kVelocity);
  bool released = false;
  for (size_t at = 0; at < total;) {
    if (!released && at >= hold) {
      for (const int note : kNotes) synth.noteOff(0, note);
      released = true;
    }
    size_t n = std::min<size_t>(size_t(kBlock), total - at);
    if (!released && at + n > hold) n = hold - at;  // the release lands on a block edge
    float* o[2] = {out.left.data() + at, out.right.data() + at};
    const float* in[2] = {effect ? inL.data() + at : nullptr, effect ? inR.data() + at : nullptr};
    synth.process(effect ? in : nullptr, o, nullptr, int(n));
    at += n;
  }
  return true;
}

std::vector<double> levelEnvelope(const Audio& audio) {
  constexpr size_t kFrame = size_t(kSampleRate / 4);
  std::vector<double> out;
  for (size_t from = 0; from + kFrame <= audio.left.size(); from += kFrame) {
    double sum = 0.0;
    for (size_t i = from; i < from + kFrame; ++i) {
      sum += double(audio.left[i]) * double(audio.left[i]) + double(audio.right[i]) * double(audio.right[i]);
    }
    out.push_back(dbOf(sum / (2.0 * double(kFrame))));
  }
  return out;
}

bool envelopesMatch(const std::vector<double>& expected, const std::vector<double>& actual, std::string& why) {
  if (expected.size() != actual.size()) {
    why = "envelope length " + std::to_string(actual.size()) + " frames, golden has " + std::to_string(expected.size());
    return false;
  }
  for (size_t i = 0; i < expected.size(); ++i) {
    if (expected[i] <= kGoldenFloorDb && actual[i] <= kGoldenFloorDb) continue;
    if (std::fabs(expected[i] - actual[i]) > kGoldenToleranceDb) {
      char buffer[160];
      std::snprintf(buffer, sizeof buffer, "at %.2f s: %.1f dB, golden %.1f dB", double(i) * 0.25, actual[i], expected[i]);
      why = buffer;
      return false;
    }
  }
  return true;
}

bool writeWav(const std::string& path, const Audio& audio, std::string& error) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    error = "cannot write " + path;
    return false;
  }
  const uint32_t frames = uint32_t(audio.left.size());
  const uint32_t dataBytes = frames * 4;
  auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) out.put(char((v >> (8 * i)) & 0xFF)); };
  auto u16 = [&](uint16_t v) { for (int i = 0; i < 2; ++i) out.put(char((v >> (8 * i)) & 0xFF)); };
  out.write("RIFF", 4);
  u32(36 + dataBytes);
  out.write("WAVEfmt ", 8);
  u32(16);
  u16(1);  // PCM
  u16(2);
  u32(uint32_t(kSampleRate));
  u32(uint32_t(kSampleRate) * 4);
  u16(4);
  u16(16);
  out.write("data", 4);
  u32(dataBytes);
  auto sample = [&](float v) {
    const float clipped = std::max(-1.0f, std::min(1.0f, v));
    u16(uint16_t(int16_t(std::lround(clipped * 32767.0f))));
  };
  for (uint32_t i = 0; i < frames; ++i) {
    sample(audio.left[i]);
    sample(audio.right[i]);
  }
  out.flush();
  if (!out) {
    error = "cannot write " + path;
    return false;
  }
  return true;
}

bool collect(const std::vector<std::string>& dirs, std::vector<Entry>& out, std::string& error) {
  out.clear();
  std::vector<std::string> bankOrder;
  for (const std::string& dir : dirs) {
    bankOrder.push_back(bankName(dir));
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) continue;
    std::vector<fs::path> files;
    for (const auto& item : fs::directory_iterator(dir, ec)) {
      if (item.is_regular_file() && item.path().extension() == ".json") files.push_back(item.path());
    }
    std::sort(files.begin(), files.end());
    for (const fs::path& path : files) {
      std::string readError;
      const std::string source = slurp(path.string(), readError);
      if (!readError.empty()) {
        error = readError;
        return false;
      }
      Entry entry;
      entry.bank = bankName(dir);
      entry.file = path.string();
      if (!compileSource(source, entry.file, entry.bank, entry.compiled, error)) return false;
      out.push_back(std::move(entry));
    }
  }
  auto bankIndex = [&](const Entry& e) {
    return std::find(bankOrder.begin(), bankOrder.end(), e.bank) - bankOrder.begin();
  };
  std::stable_sort(out.begin(), out.end(), [&](const Entry& a, const Entry& b) {
    if (bankIndex(a) != bankIndex(b)) return bankIndex(a) < bankIndex(b);
    if (a.compiled.order != b.compiled.order) return a.compiled.order < b.compiled.order;
    return a.compiled.meta.name < b.compiled.meta.name;
  });
  for (size_t i = 0; i < out.size(); ++i) {
    for (size_t j = i + 1; j < out.size(); ++j) {
      if (out[i].bank == out[j].bank && out[i].compiled.meta.name == out[j].compiled.meta.name) {
        error = out[j].file + ": the name \"" + out[j].compiled.meta.name + "\" is already used by " + out[i].file;
        return false;
      }
    }
  }
  return true;
}

std::string tableSource(const std::vector<Entry>& entries) {
  std::ostringstream out;
  out << "// Generated by eg13_preset table from presets/. Do not edit.\n"
         "#include \"preset/factory.h\"\n\n"
         "namespace eg13::preset {\n\n";
  if (!entries.empty()) {
    out << "namespace {\n\nconst FactoryPreset kTable[] = {\n";
    for (const Entry& e : entries) {
      const Compiled& c = e.compiled;
      out << "    {" << cString(e.bank) << ", " << cString(c.meta.name) << ", " << cString(c.meta.note) << ", "
          << cString(c.meta.credit) << ", " << cString(c.meta.licence) << ", "
          << (c.meta.kind == preset::Kind::Patch ? "Kind::Patch" : "Kind::Performance") << ", "
          << (c.meta.plugin == PluginKind::Effect ? "PluginKind::Effect" : "PluginKind::Instrument") << ", "
          << (c.firstLoad ? "true" : "false") << ", " << c.order << ",\n     " << longString(json::parse(c.json).dump()) << "},\n";
    }
    out << "};\n\n}  // namespace\n\n";
  }
  out << "const FactoryPreset* generatedFactoryPresets(size_t& count) {\n  count = " << entries.size() << ";\n  return "
      << (entries.empty() ? "nullptr" : "kTable") << ";\n}\n\n}  // namespace eg13::preset\n";
  return out.str();
}

bool goldenFor(const std::vector<Entry>& entries, std::string& jsonText, std::string& error) {
  json root;
  root["version"] = 1;
  root["presets"] = json::object();
  for (const Entry& e : entries) {
    Audio audio;
    if (!render(e.compiled.json, 0, kDefaultSeconds, audio, error)) {
      error = e.file + ": " + error;
      return false;
    }
    json envelope = json::array();
    for (const double db : levelEnvelope(audio)) envelope.push_back(std::round(db * 100.0) / 100.0);
    root["presets"][e.bank + "/" + e.compiled.meta.name] = {{"envelope", envelope}};
  }
  jsonText = root.dump(1) + "\n";
  return true;
}

}  // namespace eg13::presettool
