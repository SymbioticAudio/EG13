#pragma once

#include <cstddef>
#include <cstdint>

#include "synth/plugin_kind.h"

namespace eg13 {

constexpr int kNumParts = 2;
// Source P1..P16 per part. Engines use the first EngineInfo::paramCount; the
// rest are named "(unused)" and show "-".
constexpr size_t kSourceParamCount = 16;

// Value = parameter-ID index within Slot::Part. Never renumber.
enum class PartParam : uint32_t {
  Level = 1,
  Pan,
  Glide,
  Transpose,
  BaseNote,
  MidiChannel,
  KeyLow,
  KeyHigh,
  VoiceMode,
  VoiceCount,
  Trigger,
  BendRange,
  Mono,
  Output,
  InputSource,
};
constexpr size_t kPartParamCount = 15;

// Value = parameter-ID index for part 0xFF, Slot::Part. Never renumber.
enum class MasterParam : uint32_t { Level = 0, Dry, Mode, SplitNote };
constexpr size_t kMasterParamCount = 4;

enum class PartMode { Single = 0, Layer = 1, Split = 2, Multi = 3 };

// Per part: Source engine selector, P1..P16, then the part settings.
constexpr size_t kParamsPerPart = 1 + kSourceParamCount + kPartParamCount;
constexpr size_t kParamCount = size_t(kNumParts) * kParamsPerPart + kMasterParamCount;

constexpr size_t sourceEngineParamIndex(int part) { return size_t(part) * kParamsPerPart; }
constexpr size_t sourceParamIndex(int part, int p) {
  return size_t(part) * kParamsPerPart + 1 + size_t(p);
}
constexpr size_t partParamIndex(int part, PartParam param) {
  return size_t(part) * kParamsPerPart + 1 + kSourceParamCount + size_t(uint32_t(param) - 1);
}
constexpr size_t masterParamIndex(MasterParam param) {
  return size_t(kNumParts) * kParamsPerPart + size_t(uint32_t(param));
}

enum class ParamFormat {
  Engine,
  Percent,
  Pan,
  GlideTime,
  Semitones,
  BendRange,
  Note,
  Channel,
  VoiceMode,
  VoiceCount,
  Trigger,
  OnOff,
  OutputBus,
  PartMode,
  SourceEngine,
  InputSource,
};

struct ParamSpec {
  uint32_t id;
  const char* name;
  const char* module;
  float minValue;
  float maxValue;
  float defaultValue;   // instrument
  float effectDefault;  // EG13 FX
  bool stepped;
  ParamFormat format;
};

const ParamSpec& paramSpec(size_t index);  // index < kParamCount
int paramIndex(uint32_t id);               // -1 if unknown
float paramDefault(size_t index, PluginKind kind);
float clampParam(size_t index, double value);  // clamp, round stepped, NaN -> min
// sourceEngine: the part's selected engine, used for Source P1..P16.
void formatParam(size_t index, double value, char* buf, size_t size, int sourceEngine = 0);
const char* paramName(size_t index, int sourceEngine = 0);
float glideSeconds(float normalized);  // 2 * v^2 seconds

}  // namespace eg13
