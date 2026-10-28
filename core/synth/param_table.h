#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/part_fx_engine.h"
#include "engine/voice_engine.h"
#include "mod/mod_routes.h"
#include "part/part_level.h"
#include "synth/plugin_kind.h"

namespace eg13 {

constexpr int kNumParts = 2;
// Source P1..P16, Filter P1..P4, Shaper P1..P12 per part (spec §7.1; Shaper amended in M3), and
// per mod slot P1..P16 (M4). Engines use the first EngineInfo::paramCount; the rest are named
// "(unused)" and show "-".
constexpr size_t kSourceParamCount = 16;
constexpr size_t kFilterParamCount = 4;
constexpr size_t kShaperParamCount = 12;
constexpr size_t kModSlotParamCount = size_t(kModParamCount);
constexpr size_t kPartFxSlotParamCount = size_t(kPartFxParamCount);  // M5: Part FX P1..P16

// The engine slots of a part: Source, Filter, Shaper, then the four mod slots (M4), then the Part
// FX (M5). The values are table and stash order, not parameter IDs.
enum class SlotKind : int { Source = 0, Filter = 1, Shaper = 2, Mod1 = 3, Mod2 = 4, Mod3 = 5, Mod4 = 6, PartFx = 7 };
constexpr int kNumSlotKinds = 8;
constexpr bool isModSlot(SlotKind kind) { return kind >= SlotKind::Mod1 && kind <= SlotKind::Mod4; }
constexpr SlotKind modSlotKind(int slot) { return SlotKind(int(SlotKind::Mod1) + slot); }
constexpr int modSlotIndex(SlotKind kind) { return int(kind) - int(SlotKind::Mod1); }

// Value = parameter-ID index within Slot::Part. Never renumber; append only.
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
  MpeMode,       // M4: 0 Off, 1 Lower zone, 2 Upper zone
  MpeBendRange,  // M4: semitones of a member channel's full pitch bend
  CcA,           // M4: the controller numbers of the CC A..D sources
  CcB,
  CcC,
  CcD,
  PartFxInput,   // M5: 0 Part mix, 1 Input, 2 Part mix + Input
};
constexpr size_t kPartParamCount = 22;

// Value = parameter-ID index for part 0xFF, Slot::Part. Never renumber; append only.
enum class MasterParam : uint32_t { Level = 0, Dry, Mode, SplitNote, VoiceGain, SoftClip, LatencyMode };
constexpr size_t kMasterParamCount = 7;

enum class PartMode { Single = 0, Layer = 1, Split = 2, Multi = 3 };

constexpr size_t slotParamCount(SlotKind kind) {
  return kind == SlotKind::Source   ? kSourceParamCount
         : kind == SlotKind::Filter ? kFilterParamCount
         : kind == SlotKind::Shaper ? kShaperParamCount
         : kind == SlotKind::PartFx ? kPartFxSlotParamCount
                                    : kModSlotParamCount;
}
// A slot's block in a part's table: its selector, its P's, and for a mod slot its Scope.
constexpr size_t slotBlockSize(SlotKind kind) { return 1 + slotParamCount(kind) + (isModSlot(kind) ? 1 : 0); }
// Where a slot's selector sits within a part's block.
constexpr size_t slotOffset(SlotKind kind) {
  size_t offset = 0;
  for (int k = 0; k < int(kind); ++k) offset += slotBlockSize(SlotKind(k));
  return offset;
}
// Per part: the eight slot blocks, the 12 route amounts, then the part settings. Table order
// only: saved state uses parameter IDs.
constexpr size_t kSlotsBlockSize = slotOffset(SlotKind::PartFx) + slotBlockSize(SlotKind::PartFx);
constexpr size_t kParamsPerPart = kSlotsBlockSize + size_t(kNumRoutes) + kPartParamCount;
constexpr size_t kParamCount = size_t(kNumParts) * kParamsPerPart + kMasterParamCount;

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
constexpr size_t partFxEngineParamIndex(int part) { return slotEngineParamIndex(SlotKind::PartFx, part); }
constexpr size_t partFxParamIndex(int part, int p) { return slotParamIndex(SlotKind::PartFx, part, p); }
constexpr size_t modEngineParamIndex(int part, int slot) { return slotEngineParamIndex(modSlotKind(slot), part); }
constexpr size_t modParamIndex(int part, int slot, int p) { return slotParamIndex(modSlotKind(slot), part, p); }
constexpr size_t modScopeParamIndex(int part, int slot) {
  return modEngineParamIndex(part, slot) + 1 + kModSlotParamCount;
}
constexpr size_t routeAmountParamIndex(int part, int route) {
  return size_t(part) * kParamsPerPart + kSlotsBlockSize + size_t(route);
}
constexpr size_t partParamIndex(int part, PartParam param) {
  return size_t(part) * kParamsPerPart + kSlotsBlockSize + size_t(kNumRoutes) + size_t(uint32_t(param) - 1);
}
constexpr size_t masterParamIndex(MasterParam param) {
  return size_t(kNumParts) * kParamsPerPart + size_t(uint32_t(param));
}

enum class ParamFormat {
  Engine,  // a slot's P, formatted by the slot's selected engine
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
  ModEngine,    // M4
  ModScope,     // M4
  RouteAmount,  // M4: by the route's destination
  MpeMode,      // M4
  CcNumber,     // M4
  VoiceGain,    // M4
  LatencyMode,  // M5: ConverterQuality
  PartFxEngine,  // M5
  PartFxInput,   // M5
  PartLevel,     // task 8g: the knob position, shown as its gain in dB (partLevelGain)
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

// The engine selected in each slot of a part, and each route's destination, for engine-aware
// names and text.
struct PartEngines {
  int source = 0;
  int filter = 0;
  int shaper = 0;
  int mod[kNumModSlots] = {};
  uint16_t routeDestination[kNumRoutes] = {};
  int partFx = 0;  // M5
};

int slotEngineCount(SlotKind kind);
int slotEngineFromSelector(SlotKind kind, double value);  // unknown -> 0 (Macro / Off)
const EngineInfo& slotEngineInfo(SlotKind kind, int engine);
// If index is a slot's P: its slot, part and P (0-based).
bool slotParamOf(size_t index, SlotKind* kind, int* part, int* p);
// If index is a slot's engine selector: its slot and part.
bool slotSelectorOf(size_t index, SlotKind* kind, int* part);
// If index is a mod slot's Scope: its part and slot (0..3).
bool modScopeOf(size_t index, int* part, int* slot);
// If index is a route amount: its part and route (0..11).
bool routeAmountOf(size_t index, int* part, int* route);

// The init patches' routes (spec §6), patch data the Synth starts from, and their amounts (the
// route amounts' host defaults).
Route initPatchRoute(PluginKind plugin, int part, int route);
float initPatchAmount(PluginKind plugin, int part, int route);

const ParamSpec& paramSpec(size_t index);  // index < kParamCount
int paramIndex(uint32_t id);               // -1 if unknown
float paramDefault(size_t index, PluginKind kind);
float clampParam(size_t index, double value);  // clamp, round stepped, NaN -> min
// engines: the part's selected engines and route destinations, for slot P and route text.
void formatParam(size_t index, double value, char* buf, size_t size, const PartEngines& engines = {});
void formatParam(size_t index, double value, char* buf, size_t size, int sourceEngine);
const char* paramName(size_t index, const PartEngines& engines = {});
const char* paramName(size_t index, int sourceEngine);
float glideSeconds(float normalized);  // 2 * v^2 seconds

}  // namespace eg13
