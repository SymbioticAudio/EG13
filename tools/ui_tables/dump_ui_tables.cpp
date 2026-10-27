// Writes the engine tables the editor's previews and tests use as JSON: every slot's engines, the
// names of their P's, and the value strings each P shows (EngineParamInfo::format swept over 0..1).
//   eg13_ui_tables <output.json>
// The file is committed as ui/EG13UI/Sources/EG13UIFixtures/core-tables.json; the ctest
// ui_tables_up_to_date regenerates it and diffs. The output is deterministic: fixed key order
// (sorted), only strings and integers, no floats, and a fixed order for the strings.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "common/engine_names.h"
#include "synth/param_table.h"

namespace {

using eg13::EngineInfo;
using eg13::SlotKind;

// A P lists every string it can show, except a numeric one (percent, Hz, dB, st, ms, Q, ...):
// those run to hundreds of strings that differ only in their digits. A P is numeric when it shows
// more than kMaxListed strings and they fall into at most kMaxTemplates shapes (the strings with
// each digit replaced by '#': "+#.# st", "+##.# st"). A numeric P lists one string of each
// of its kLongest longest shapes (longest first, ties in shape order): the ones that can overflow
// a knob's label. Enumerations and steps always list everything, in the order the sweep shows them.
constexpr size_t kMaxListed = 40;
constexpr size_t kMaxTemplates = 12;
constexpr size_t kLongest = 3;

std::string escape(const std::string& text) {
  std::string out;
  for (const char c : text) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (c == '"') out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if (u < 0x20) {
      char buf[8];
      std::snprintf(buf, sizeof buf, "\\u%04x", unsigned(u));
      out += buf;
    } else {
      out += c;
    }
  }
  return out;
}

std::string quoted(const std::string& text) { return "\"" + escape(text) + "\""; }

// "+12.5 st" -> "+##.# st": the shape of a number's text.
std::string shapeOf(const std::string& text) {
  std::string shape;
  for (const char c : text) {
    shape += c >= '0' && c <= '9' ? '#' : c;
  }
  return shape;
}

std::vector<std::string> valueStrings(const eg13::EngineParamInfo& param) {
  const std::vector<std::string> ordered = eg13::tools::sweepStrings(param);  // first appearance
  if (ordered.size() <= kMaxListed) return ordered;
  std::vector<std::pair<std::string, std::string>> byShape;  // (shape, first string of that shape)
  std::set<std::string> shapes;
  for (const std::string& text : ordered) {
    if (shapes.insert(shapeOf(text)).second) byShape.emplace_back(shapeOf(text), text);
  }
  if (byShape.size() > kMaxTemplates) return ordered;  // an enumeration with many names
  std::sort(byShape.begin(), byShape.end(), [](const auto& a, const auto& b) {
    return a.first.size() != b.first.size() ? a.first.size() > b.first.size() : a.first < b.first;
  });
  if (byShape.size() > kLongest) byShape.resize(kLongest);
  std::vector<std::string> out;
  for (const auto& entry : byShape) out.push_back(entry.second);
  return out;
}

void writeEngine(std::ostream& out, const EngineInfo& info, const char* indent) {
  out << indent << "{\n" << indent << "  \"name\": " << quoted(info.displayName) << ",\n";
  out << indent << "  \"params\": [" << (info.paramCount ? "" : "]\n");
  for (int p = 0; p < info.paramCount; ++p) {
    out << (p ? ",\n" : "\n") << indent << "    {\"name\": " << quoted(info.params[p].name) << ", \"values\": [";
    const std::vector<std::string> values = valueStrings(info.params[p]);
    for (size_t v = 0; v < values.size(); ++v) out << (v ? ", " : "") << quoted(values[v]);
    out << "]}";
  }
  if (info.paramCount) out << "\n" << indent << "  ]\n";
  out << indent << "}";
}

void writeSlot(std::ostream& out, const char* key, SlotKind kind, bool last) {
  out << "    " << quoted(key) << ": [\n";
  const int count = eg13::slotEngineCount(kind);
  for (int e = 0; e < count; ++e) {
    writeEngine(out, eg13::slotEngineInfo(kind, e), "      ");
    out << (e + 1 < count ? ",\n" : "\n");
  }
  out << "    ]" << (last ? "\n" : ",\n");
}

// The four mod slots share one engine table; the file lists it once.
bool modSlotsAgree() {
  for (int slot = 1; slot < eg13::kNumModSlots; ++slot) {
    const SlotKind kind = eg13::modSlotKind(slot);
    if (eg13::slotEngineCount(kind) != eg13::slotEngineCount(SlotKind::Mod1)) return false;
    for (int e = 0; e < eg13::slotEngineCount(kind); ++e) {
      if (&eg13::slotEngineInfo(kind, e) != &eg13::slotEngineInfo(SlotKind::Mod1, e)) return false;
    }
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: eg13_ui_tables <output.json>\n";
    return 2;
  }
  if (!modSlotsAgree()) {
    std::cerr << "eg13_ui_tables: the mod slots no longer share one engine table; list each\n";
    return 3;
  }
  std::ofstream out(argv[1], std::ios::binary | std::ios::trunc);
  if (!out) {
    std::cerr << "eg13_ui_tables: cannot write " << argv[1] << "\n";
    return 1;
  }
  out << "{\n  \"slots\": {\n";
  // Sorted keys.
  writeSlot(out, "filter", SlotKind::Filter, false);
  writeSlot(out, "mod", SlotKind::Mod1, false);
  writeSlot(out, "partFx", SlotKind::PartFx, false);
  writeSlot(out, "shaper", SlotKind::Shaper, false);
  writeSlot(out, "source", SlotKind::Source, true);
  out << "  },\n  \"version\": 1\n}\n";
  out.flush();
  return out ? 0 : 1;
}
