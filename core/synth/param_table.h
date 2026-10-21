#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/voice_engine.h"
#include "synth/plugin_kind.h"

namespace eg13 {

constexpr int kNumParts = 2;
// Source P1..P16, Filter P1..P4, Shaper P1..P12 per part (spec §7.1; Shaper amended in M3).
// Engines use the first EngineInfo::paramCount; the rest are named "(unused)" and show "-".
constexpr size_t kSourceParamCount = 16;
constexpr size_t kFilterParamCount = 4;
constexpr size_t kShaperParamCount = 12;

enum class SlotKind : int { Source = 0, Filter = 1, Shaper = 2 };
constexpr int kNumSlotKinds = 3;

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

// Per part: Source selector + P1..P16, Filter selector + P1..P4, Shaper selector + P1..P12,
// then the part settings. Table order only: saved state uses parameter IDs.
constexpr size_t kParamsPerPart =
    3 + kSourceParamCount + kFilterParamCount + kShaperParamCount + kPartParamCount;
constexpr size_t kParamCount = size_t(kNumParts) * kParamsPerPart + kMasterParamCount;

constexpr size_t slotParamCount(SlotKind kind) {
  return kind == SlotKind::Source   ? kSourceParamCount
         : kind == SlotKind::Filter ? kFilterParamCount
                                    : kShaperParamCount;
}
// Where a slot's selector sits within a part's block.
constexpr size_t slotOffset(SlotKind kind) {
  return kind == SlotKind::Source   ? 0
         : kind == SlotKind::Filter ? 1 + kSourceParamCount
                                    : 2 + kSourceParamCount + kFilterParamCount;
}
constexpr size_t slotEngineParamIndex(SlotKind kind, int part) {
  return size_t(part) * kParamsPerPart + slotOffset(kind);
}
constexpr size_t slotParamIndex(SlotKind kind, int part, int p) {
  return slotEngineParamIndex(kind, part) + 1 + size_t(p);
}
constexpr size_t sourceEngineParamIndex(int part) { return slotEngineParamIndex(SlotKind::Source, part); }
constexpr size_t sourceParamIndex(int part, int p) { return slotParamIndex(SlotKind::Source, part, p); }
constexpr size_t filterEngineParamIndex(int part) { return slotEngineParamIndex(SlotKind::Filter, part); }
constexpr size_t filterParamIndex(int part, int p) { return slotParamIndex(SlotKind::Filter, part, p); }
constexpr size_t shaperEngineParamIndex(int part) { return slotEngineParamIndex(SlotKind::Shaper, part); }
constexpr size_t shaperParamIndex(int part, int p) { return slotParamIndex(SlotKind::Shaper, part, p); }
constexpr size_t partParamIndex(int part, PartParam param) {
  return size_t(part) * kParamsPerPart + 3 + kSourceParamCount + kFilterParamCount + kShaperParamCount +
         size_t(uint32_t(param) - 1);
}
constexpr size_t masterParamIndex(MasterParam param) {
  return size_t(kNumParts) * kParamsPerPart + size_t(uint32_t(param));
}

enum class ParamFormat {
  Engine,  // a Source/Filter/Shaper P, formatted by the slot's selected engine
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
  FilterEngine,
  ShaperEngine,
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

// The engine selected in each slot of a part, for engine-aware names and text.
struct PartEngines {
  int source = 0;
  int filter = 0;
  int shaper = 0;
};

int slotEngineCount(SlotKind kind);
int slotEngineFromSelector(SlotKind kind, double value);  // unknown -> 0 (Macro / Off / Off)
const EngineInfo& slotEngineInfo(SlotKind kind, int engine);
// If index is a Source/Filter/Shaper P: its slot, part and P (0-based).
bool slotParamOf(size_t index, SlotKind* kind, int* part, int* p);
// If index is a slot's engine selector: its slot and part.
bool slotSelectorOf(size_t index, SlotKind* kind, int* part);

const ParamSpec& paramSpec(size_t index);  // index < kParamCount
int paramIndex(uint32_t id);               // -1 if unknown
float paramDefault(size_t index, PluginKind kind);
float clampParam(size_t index, double value);  // clamp, round stepped, NaN -> min
// engines: the part's selected engines, used for Source/Filter/Shaper P names and text.
void formatParam(size_t index, double value, char* buf, size_t size, const PartEngines& engines = {});
void formatParam(size_t index, double value, char* buf, size_t size, int sourceEngine);
const char* paramName(size_t index, const PartEngines& engines = {});
const char* paramName(size_t index, int sourceEngine);
float glideSeconds(float normalized);  // 2 * v^2 seconds

}  // namespace eg13
