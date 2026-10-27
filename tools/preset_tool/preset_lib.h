// The preset tool's library: readable preset sources to preset files, back again, the fixed note
// script that renders them, and the golden level envelopes. Used by eg13_preset and the tests.
// Plain C++ over eg13_core (std::filesystem allowed here, never in /core).
#pragma once

#include <string>
#include <vector>

#include "preset/preset.h"

namespace eg13::presettool {

constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 128;
constexpr double kDefaultSeconds = 12.0;
constexpr double kGoldenToleranceDb = 1.5;
constexpr double kGoldenFloorDb = -60.0;

// A compiled source: the preset file text plus the list fields that live only in the source.
struct Compiled {
  preset::Meta meta;
  int order = 0;
  bool firstLoad = false;
  std::string json;  // the preset file
};

// Source text to a complete preset file (an Init synth of the source's plugin with the source's
// settings applied, saved). `file` labels errors; `bank` goes into the file. False with an error
// naming the file and the offending name.
bool compileSource(const std::string& text, const std::string& file, const std::string& bank, Compiled& out,
                   std::string& error);

// The listing fields of a source or a preset file as "key=value" lines: name, note, credit, plugin,
// kind, order (0 when absent). Control characters in values become spaces, so a line is a field.
bool presetInfo(const std::string& text, std::string& lines, std::string& error);

// A preset file to source text (every P of the selected engines, all settings, routes in use).
// Envelope shapes are not part of the source format and are dropped.
bool decompile(const std::string& presetJson, std::string& sourceText, std::string& error);

struct Audio {
  std::vector<float> left, right;
};

// The note script on a fresh Synth of the preset's plugin kind with the preset applied (a Patch
// into `part`, 0 or 1): C3 G3 D4 A4 at velocity 100 held 8 s, released, then a tail, `seconds` in
// all.
bool render(const std::string& presetJson, int part, double seconds, Audio& out, std::string& error);

// RMS in dB (both channels) per 250 ms.
std::vector<double> levelEnvelope(const Audio& audio);
// Frames louder than the floor in either envelope must agree to within the tolerance, and the
// envelopes must be the same length. Says where it fails.
bool envelopesMatch(const std::vector<double>& expected, const std::vector<double>& actual, std::string& why);

// 16-bit PCM, 48 kHz stereo.
bool writeWav(const std::string& path, const Audio& audio, std::string& error);

// The factory table's contents, from preset source directories (a missing directory is empty).
// Banks: bank1 "Bank 1", bank2-catnap "Bank 2", fx "FX". Sorted by bank (as listed), order, name.
struct Entry {
  std::string bank;
  std::string file;
  Compiled compiled;
};
bool collect(const std::vector<std::string>& dirs, std::vector<Entry>& out, std::string& error);
std::string tableSource(const std::vector<Entry>& entries);  // the generated .cpp

// tests/data/preset_golden.json for the entries; key "<bank>/<name>".
bool goldenFor(const std::vector<Entry>& entries, std::string& jsonText, std::string& error);

}  // namespace eg13::presettool
