// The preset tool. Readable preset sources (presets/README.md) to preset files, the embedded
// factory table, WAV renders for listening and the golden level envelopes.
//   eg13_preset compile <source.json> -o <file> [--bank <name>] [--name <name>]
//   eg13_preset info <source.json|file>
//   eg13_preset decompile <file> -o <source.json>
//   eg13_preset render <source.json|file> -o <out.wav> [--seconds 12] [--part A|B] [--stress | --extremes] [--no-soft-clip]
//   eg13_preset golden [--write] [--presets <dir>] [--file <preset_golden.json>]
//   eg13_preset table <dir>... -o <factory_presets.cpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "preset_lib.h"

using namespace eg13::presettool;

namespace {

int usage() {
  std::cerr << "usage:\n"
               "  eg13_preset compile <source.json> -o <file> [--bank <name>] [--name <name>]\n"
               "  eg13_preset info <source.json|file>\n"
               "  eg13_preset decompile <file> -o <source.json>\n"
               "  eg13_preset render <source.json|file> -o <out.wav> [--seconds 12] [--part A|B] [--stress | --extremes] [--no-soft-clip]\n"
               "  eg13_preset golden [--write] [--presets <dir>] [--file <preset_golden.json>]\n"
               "  eg13_preset table <dir>... -o <factory_presets.cpp>\n";
  return 2;
}

bool read(const std::string& path, std::string& text) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    std::cerr << "eg13_preset: cannot read " << path << "\n";
    return false;
  }
  std::ostringstream out;
  out << in.rdbuf();
  text = out.str();
  return true;
}

bool write(const std::string& path, const std::string& text) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << text;
  out.flush();
  if (!out) {
    std::cerr << "eg13_preset: cannot write " << path << "\n";
    return false;
  }
  return true;
}

struct Args {
  std::vector<std::string> positional;
  std::string output, bank, name, presets = EG13_PRESETS_DIR, file = EG13_GOLDEN_FILE, part = "A";
  double seconds = kDefaultSeconds;
  bool writeGolden = false;
  bool stress = false;      // render: the headroom script (stressScript) instead of the note script
  bool extremes = false;    // render: the expression extremes script (extremesScript)
  bool noSoftClip = false;  // render: master Soft Clip off, and print the raw peak
  bool ok = true;
};

Args parse(int argc, char** argv) {
  Args a;
  for (int i = 2; i < argc; ++i) {
    const std::string arg = argv[i];
    auto value = [&](std::string& into) {
      if (i + 1 >= argc) {
        a.ok = false;
        return;
      }
      into = argv[++i];
    };
    if (arg == "-o") value(a.output);
    else if (arg == "--bank") value(a.bank);
    else if (arg == "--name") value(a.name);
    else if (arg == "--presets") value(a.presets);
    else if (arg == "--file") value(a.file);
    else if (arg == "--part") value(a.part);
    else if (arg == "--seconds") {
      std::string s;
      value(s);
      a.seconds = std::atof(s.c_str());
      if (!(a.seconds > 0.0 && a.seconds <= 600.0)) a.ok = false;
    } else if (arg == "--write") a.writeGolden = true;
    else if (arg == "--stress") a.stress = true;
    else if (arg == "--extremes") a.extremes = true;
    else if (arg == "--no-soft-clip") a.noSoftClip = true;
    else if (!arg.empty() && arg[0] == '-') a.ok = false;
    else a.positional.push_back(arg);
  }
  return a;
}

int fail(const std::string& message) {
  std::cerr << "eg13_preset: " << message << "\n";
  return 1;
}

int compile(const Args& a) {
  std::string source, error;
  if (a.positional.size() != 1 || a.output.empty()) return usage();
  if (!read(a.positional[0], source)) return 1;
  if (!a.name.empty()) {  // --name: the file carries this name instead of the source's
    nlohmann::json root = nlohmann::json::parse(source, nullptr, false);
    if (!root.is_object()) return fail(a.positional[0] + ": not JSON");
    root["name"] = a.name;
    source = root.dump();
  }
  Compiled compiled;
  if (!compileSource(source, a.positional[0], a.bank, compiled, error)) return fail(error);
  return write(a.output, compiled.json) ? 0 : 1;
}

int infoCommand(const Args& a) {
  std::string text, lines, error;
  if (a.positional.size() != 1) return usage();
  if (!read(a.positional[0], text)) return 1;
  if (!presetInfo(text, lines, error)) return fail(a.positional[0] + ": " + error);
  std::cout << lines;
  return 0;
}

int decompileCommand(const Args& a) {
  std::string preset, source, error;
  if (a.positional.size() != 1 || a.output.empty()) return usage();
  if (!read(a.positional[0], preset)) return 1;
  if (!decompile(preset, source, error)) return fail(a.positional[0] + ": " + error);
  return write(a.output, source) ? 0 : 1;
}

int renderCommand(const Args& a) {
  std::string text, error;
  if (a.positional.size() != 1 || a.output.empty() || (a.part != "A" && a.part != "B")) return usage();
  if (!read(a.positional[0], text)) return 1;
  // A preset file or a source: a file carries "eg13Preset".
  const nlohmann::json root = nlohmann::json::parse(text, nullptr, false);
  if (!root.is_object()) return fail(a.positional[0] + ": not JSON");
  std::string presetJson = text;
  if (!root.contains("eg13Preset")) {
    Compiled compiled;
    if (!compileSource(text, a.positional[0], "", compiled, error)) return fail(error);
    presetJson = compiled.json;
  }
  Audio audio;
  RenderOptions options;
  options.softClip = !a.noSoftClip;
  if (a.stress && a.extremes) return usage();
  const NoteScript script = a.stress ? stressScript() : a.extremes ? extremesScript() : noteScript(a.seconds);
  if (!renderScript(presetJson, a.part == "B" ? 1 : 0, script, audio, error, &options)) return fail(error);
  if (a.noSoftClip) {  // the WAV is 16-bit and clips; the raw peak is printed
    float peak = 0.0f;
    for (size_t i = 0; i < audio.left.size(); ++i) peak = std::max({peak, std::abs(audio.left[i]), std::abs(audio.right[i])});
    std::printf("peak_dbfs=%.2f\n", 20.0 * std::log10(double(peak) + 1e-12));
  }
  if (!writeWav(a.output, audio, error)) return fail(error);
  return 0;
}

std::vector<std::string> factoryDirs(const std::string& root) {
  return {root + "/bank1", root + "/bank2-catnap", root + "/fx"};
}

int golden(const Args& a) {
  std::vector<Entry> entries;
  std::string error, text;
  if (!collect(factoryDirs(a.presets), entries, error)) return fail(error);
  if (!goldenFor(entries, text, error)) return fail(error);
  if (a.writeGolden) {
    if (!write(a.file, text)) return 1;
    std::cout << "wrote " << entries.size() << " golden envelopes to " << a.file << "\n";
    return 0;
  }
  std::string stored;
  if (!read(a.file, stored)) return 1;
  const nlohmann::json want = nlohmann::json::parse(stored, nullptr, false);
  const nlohmann::json have = nlohmann::json::parse(text);
  if (!want.is_object() || !want.contains("presets")) return fail(a.file + ": not a golden file");
  int bad = 0;
  for (auto it = have["presets"].begin(); it != have["presets"].end(); ++it) {
    std::string why;
    if (!want["presets"].contains(it.key())) {
      std::cerr << it.key() << ": not in the golden file\n";
      ++bad;
    } else if (!envelopesMatch(want["presets"][it.key()]["envelope"].get<std::vector<double>>(),
                               it.value()["envelope"].get<std::vector<double>>(), why)) {
      std::cerr << it.key() << ": " << why << "\n";
      ++bad;
    }
  }
  for (auto it = want["presets"].begin(); it != want["presets"].end(); ++it) {
    if (!have["presets"].contains(it.key())) {
      std::cerr << it.key() << ": in the golden file but no longer a preset\n";
      ++bad;
    }
  }
  if (bad) return fail(std::to_string(bad) + " preset(s) differ; review, then `eg13_preset golden --write`");
  return 0;
}

int table(const Args& a) {
  if (a.output.empty()) return usage();
  std::vector<Entry> entries;
  std::string error;
  if (!collect(a.positional, entries, error)) return fail(error);
  // Rewritten only when it changes, so an unchanged table does not recompile.
  const std::string source = tableSource(entries);
  std::string current;
  std::ifstream existing(a.output, std::ios::binary);
  if (existing) {
    std::ostringstream text;
    text << existing.rdbuf();
    current = text.str();
  }
  if (existing && current == source) return 0;
  return write(a.output, source) ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return usage();
  const std::string command = argv[1];
  const Args a = parse(argc, argv);
  if (!a.ok) return usage();
  if (command == "compile") return compile(a);
  if (command == "info") return infoCommand(a);
  if (command == "decompile") return decompileCommand(a);
  if (command == "render") return renderCommand(a);
  if (command == "golden") return golden(a);
  if (command == "table") return table(a);
  return usage();
}
