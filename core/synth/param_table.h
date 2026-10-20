#pragma once

#include <cstddef>
#include <cstdint>

#include "synth/plugin_kind.h"

namespace eg13 {

constexpr int kNumParts = 2;
constexpr size_t kSourceParamCount = 8;

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
};
constexpr size_t kPartParamCount = 14;

// Value = parameter-ID index for part 0xFF, Slot::Part. Never renumber.
enum class MasterParam : uint32_t { Level = 0, Dry, Mode, SplitNote };
constexpr size_t kMasterParamCount = 4;

enum class PartMode { Single = 0, Layer = 1, Split = 2, Multi = 3 };

constexpr size_t kParamsPerPart = kSourceParamCount + kPartParamCount;
constexpr size_t kParamCount = size_t(kNumParts) * kParamsPerPart + kMasterParamCount;

constexpr size_t sourceParamIndex(int part, int p) {
  return size_t(part) * kParamsPerPart + size_t(p);
}
constexpr size_t partParamIndex(int part, PartParam param) {
  return size_t(part) * kParamsPerPart + kSourceParamCount + size_t(uint32_t(param) - 1);
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
void formatParam(size_t index, double value, char* buf, size_t size);
float glideSeconds(float normalized);  // 2 * v^2 seconds

}  // namespace eg13
