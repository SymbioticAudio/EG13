#include "synth/param_table.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include "engine/engine_registry.h"
#include "engine/shelves_engine.h"
#include "engine/sst_filter_engine.h"
#include "engine/warps_engine.h"
#include "params/param_ids.h"

namespace eg13 {

static_assert(kSourceParamCount <= size_t(kMaxSlotParams), "ParamValues must hold every Source P");
static_assert(kShaperParamCount <= size_t(kMaxSlotParams), "ParamValues must hold every Shaper P");

namespace {

const char* const kSlotModules[kNumSlotKinds][kNumParts] = {
    {"Part A/Source", "Part B/Source"},
    {"Part A/Filter", "Part B/Filter"},
    {"Part A/Shaper", "Part B/Shaper"},
};
const char* const kPartModules[kNumParts] = {"Part A", "Part B"};
const SlotKind kSlotKinds[kNumSlotKinds] = {SlotKind::Source, SlotKind::Filter, SlotKind::Shaper};

struct PartParamDef {
  PartParam param;
  const char* name;
  float minValue;
  float maxValue;
  float defaultValue;
  bool stepped;
  ParamFormat format;
};

const PartParamDef kPartDefs[kPartParamCount] = {
    {PartParam::Level, "Level", 0, 1, 1, false, ParamFormat::Percent},
    {PartParam::Pan, "Pan", -1, 1, 0, false, ParamFormat::Pan},
    {PartParam::Glide, "Glide", 0, 1, 0, false, ParamFormat::GlideTime},
    {PartParam::Transpose, "Transpose", -24, 24, 0, true, ParamFormat::Semitones},
    {PartParam::BaseNote, "Base Note", 0, 127, 60, true, ParamFormat::Note},
    {PartParam::MidiChannel, "MIDI Channel", 0, 16, 0, true, ParamFormat::Channel},
    {PartParam::KeyLow, "Key Low", 0, 127, 0, true, ParamFormat::Note},
    {PartParam::KeyHigh, "Key High", 0, 127, 127, true, ParamFormat::Note},
    {PartParam::VoiceMode, "Voice Mode", 0, 2, 0, true, ParamFormat::VoiceMode},
    {PartParam::VoiceCount, "Voices", 1, 8, 8, true, ParamFormat::VoiceCount},
    {PartParam::Trigger, "Trigger", 0, 1, 0, true, ParamFormat::Trigger},
    {PartParam::BendRange, "Bend Range", 0, 24, 2, true, ParamFormat::BendRange},
    {PartParam::Mono, "Mono", 0, 1, 0, true, ParamFormat::OnOff},
    {PartParam::Output, "Output", 0, 2, 0, true, ParamFormat::OutputBus},
    {PartParam::InputSource, "Input Source", 0, 2, 0, true, ParamFormat::InputSource},
};

params::Slot idSlot(SlotKind kind) {
  return kind == SlotKind::Source   ? params::Slot::Source
         : kind == SlotKind::Filter ? params::Slot::Filter
                                    : params::Slot::Shaper;
}

// Default engine per slot, part and plugin. EG13 FX's Part A is its init patch's chain (spec §6:
// Audio Input -> Analog Filter); everything else starts on the slot's first engine (Macro
// Oscillator, Off, Off). The instrument init patch arrives with modulation in M4.
int defaultEngine(SlotKind kind, int part, PluginKind plugin) {
  if (plugin == PluginKind::Effect && part == 0) {
    if (kind == SlotKind::Source) return int(SourceEngine::AudioInput);
    if (kind == SlotKind::Filter) return int(FilterEngine::Analog);
  }
  return 0;
}

// Host defaults of the Filter and Shaper P's (final review I1; controller ruling, provisional).
// CLAP reports one default per parameter and cannot change it while active, so a host's
// reset-to-default sets the same value whatever engine is selected. Each value is chosen so
// that a slot reset as a whole gives a sensible, audible result on every engine of the slot
// (tests/test_levels.cpp, "Host defaults ..."). Engine switching still loads each engine's own
// stash and EngineInfo defaults; these values are only what the host shows as "default" and
// what a fresh project holds in a slot's P's.
//
// Filter: every filter's own defaults where they agree. P1 fully open (all three filters
// default to it), P2 no resonance (the Analog Filter's and Classic Filters' default; the SVF
// reads it as Q 0.5, flat with a soft knee, rather than its own 0.707), P3 the first option
// (Analog Filter LP4, Classic Filters type 0 = 12 dB low-pass, SVF LP), P4 Character centred.
const float kFilterHostDefaults[kFilterParamCount] = {
    1.0f, 0.0f, 0.5f / float(SstFilterEngine::kTypeSteps), 0.5f};
// Shaper: the EQ flat (every gain 0 dB, pad off, main output) on P2, P4, P7, P10, P11, P12;
// where the EQ does not care (frequencies and Qs while their gain is 0 dB) the Ring Mod /
// Vocoder's own defaults: P1 Algorithm Analog RM, P3 Carrier Level 0.47, P5 Carrier Internal
// Osc, P6 Modulator Voice, P8 Osc Shape first, P9 Mode Modulation. The Ring Mod's other
// controls then sit at the EQ's 0.5: Timbre 0.5 and Osc Pitch 0 st (its own defaults) and Mod
// Level 0.5 (its default is 0.47, about 0.4 dB lower).
const float kShaperHostDefaults[kShaperParamCount] = {
    0.25f,
    0.5f,
    WarpsEngine::kDefaultLevel,
    0.5f,
    0.5f / float(WarpsEngine::kSelectorSteps),
    0.5f / float(WarpsEngine::kSelectorSteps),
    0.5f,
    0.5f / float(WarpsEngine::kNumOscillatorModes),
    0.25f,
    0.5f,
    0.25f,
    0.5f / float(ShelvesEngine::kNumOutputs),
};

float hostDefault(SlotKind kind, int p, const EngineInfo& engine) {
  if (kind == SlotKind::Filter) return kFilterHostDefaults[p];
  if (kind == SlotKind::Shaper) return kShaperHostDefaults[p];
  return p < engine.paramCount ? engine.params[p].defaultValue : 0.0f;  // Source: M2's rule
}

int engineFor(SlotKind kind, const PartEngines& e) {
  return kind == SlotKind::Source ? e.source : kind == SlotKind::Filter ? e.filter : e.shaper;
}

const std::array<ParamSpec, kParamCount>& table() {
  static const std::array<ParamSpec, kParamCount> specs = [] {
    std::array<ParamSpec, kParamCount> t{};
    for (int part = 0; part < kNumParts; ++part) {
      for (SlotKind kind : kSlotKinds) {
        const int instrument = defaultEngine(kind, part, PluginKind::Instrument);
        const int effect = defaultEngine(kind, part, PluginKind::Effect);
        const ParamFormat selectorFormat = kind == SlotKind::Source   ? ParamFormat::SourceEngine
                                           : kind == SlotKind::Filter ? ParamFormat::FilterEngine
                                                                      : ParamFormat::ShaperEngine;
        const float selectorMax = float(kind == SlotKind::Source   ? kSourceEngineSelectorMax
                                        : kind == SlotKind::Filter ? kFilterEngineSelectorMax
                                                                   : kShaperEngineSelectorMax);
        const char* module = kSlotModules[int(kind)][part];
        t[slotEngineParamIndex(kind, part)] = {params::id(uint32_t(part), idSlot(kind), 0),
                                               kind == SlotKind::Source ? "Engine" : "Type",
                                               module, 0.0f, selectorMax, float(instrument), float(effect),
                                               true, selectorFormat};
        const EngineInfo& inst = slotEngineInfo(kind, instrument);
        const EngineInfo& fx = slotEngineInfo(kind, effect);
        for (int p = 0; p < int(slotParamCount(kind)); ++p) {
          const bool used = p < inst.paramCount;
          t[slotParamIndex(kind, part, p)] = {params::id(uint32_t(part), idSlot(kind), uint32_t(p + 1)),
                                              used ? inst.params[p].name : "(unused)", module, 0.0f, 1.0f,
                                              hostDefault(kind, p, inst), hostDefault(kind, p, fx), false,
                                              ParamFormat::Engine};
        }
      }
      for (const PartParamDef& d : kPartDefs) {
        t[partParamIndex(part, d.param)] = {params::id(uint32_t(part), params::Slot::Part, uint32_t(d.param)),
                                            d.name, kPartModules[part], d.minValue, d.maxValue,
                                            d.defaultValue, d.defaultValue, d.stepped, d.format};
      }
    }
    t[partParamIndex(0, PartParam::Trigger)].effectDefault = 1.0f;  // EG13 FX: Part A always on
    auto master = [](MasterParam m) {
      return params::id(params::kMasterPart, params::Slot::Part, uint32_t(m));
    };
    t[masterParamIndex(MasterParam::Level)] = {master(MasterParam::Level), "Level", "Master", 0, 1,
                                               0.8f, 1.0f, false, ParamFormat::Percent};
    t[masterParamIndex(MasterParam::Dry)] = {master(MasterParam::Dry), "Dry", "Master", 0, 1, 0, 0,
                                             false, ParamFormat::Percent};
    t[masterParamIndex(MasterParam::Mode)] = {master(MasterParam::Mode), "Mode", "Master", 0, 3, 0, 0,
                                              true, ParamFormat::PartMode};
    t[masterParamIndex(MasterParam::SplitNote)] = {master(MasterParam::SplitNote), "Split Note",
                                                   "Master", 0, 127, 60, 60, true, ParamFormat::Note};
    return t;
  }();
  return specs;
}

const char* pick(const char* const* names, int count, float value) {
  const int i = std::clamp(int(std::lround(value)), 0, count - 1);
  return names[i];
}

}  // namespace

int slotEngineCount(SlotKind kind) {
  return kind == SlotKind::Source   ? kNumSourceEngines
         : kind == SlotKind::Filter ? kNumFilterEngines
                                    : kNumShaperEngines;
}

int slotEngineFromSelector(SlotKind kind, double value) {
  return kind == SlotKind::Source   ? sourceEngineFromSelector(value)
         : kind == SlotKind::Filter ? filterEngineFromSelector(value)
                                    : shaperEngineFromSelector(value);
}

const EngineInfo& slotEngineInfo(SlotKind kind, int engine) {
  return kind == SlotKind::Source   ? sourceEngineInfo(engine)
         : kind == SlotKind::Filter ? filterEngineInfo(engine)
                                    : shaperEngineInfo(engine);
}

bool slotParamOf(size_t index, SlotKind* kind, int* part, int* p) {
  if (index >= size_t(kNumParts) * kParamsPerPart) return false;
  const size_t local = index % kParamsPerPart;
  for (SlotKind k : kSlotKinds) {
    const size_t first = slotOffset(k) + 1;
    if (local >= first && local < first + slotParamCount(k)) {
      *kind = k;
      *part = int(index / kParamsPerPart);
      *p = int(local - first);
      return true;
    }
  }
  return false;
}

bool slotSelectorOf(size_t index, SlotKind* kind, int* part) {
  if (index >= size_t(kNumParts) * kParamsPerPart) return false;
  const size_t local = index % kParamsPerPart;
  for (SlotKind k : kSlotKinds) {
    if (local == slotOffset(k)) {
      *kind = k;
      *part = int(index / kParamsPerPart);
      return true;
    }
  }
  return false;
}

const ParamSpec& paramSpec(size_t index) { return table()[index]; }

int paramIndex(uint32_t id) {
  const auto& t = table();
  for (size_t i = 0; i < kParamCount; ++i) {
    if (t[i].id == id) return int(i);
  }
  return -1;
}

float paramDefault(size_t index, PluginKind kind) {
  const ParamSpec& s = paramSpec(index);
  return kind == PluginKind::Effect ? s.effectDefault : s.defaultValue;
}

float clampParam(size_t index, double value) {
  const ParamSpec& s = paramSpec(index);
  if (std::isnan(value)) return s.minValue;
  double v = std::clamp(value, double(s.minValue), double(s.maxValue));
  if (s.stepped) v = std::round(v);
  return float(v);
}

float glideSeconds(float normalized) { return 2.0f * normalized * normalized; }

void formatParam(size_t index, double value, char* buf, size_t size, int sourceEngine) {
  PartEngines engines;
  engines.source = sourceEngine;
  formatParam(index, value, buf, size, engines);
}

void formatParam(size_t index, double value, char* buf, size_t size, const PartEngines& engines) {
  static const char* const kNotes[12] = {"C", "C#", "D", "D#", "E", "F",
                                         "F#", "G", "G#", "A", "A#", "B"};
  static const char* const kVoiceModes[3] = {"Poly", "Mono", "Legato"};
  static const char* const kTriggers[2] = {"MIDI", "Always on"};
  static const char* const kOnOff[2] = {"Off", "On"};
  static const char* const kOutputs[3] = {"Main", "Bus 2", "Other part"};
  static const char* const kModes[4] = {"Single", "Layer", "Split", "Multi"};
  static const char* const kInputSources[3] = {"External", "Other part", "Both"};

  const ParamSpec& s = paramSpec(index);
  const float v = clampParam(index, value);
  switch (s.format) {
    case ParamFormat::Engine: {
      SlotKind kind = SlotKind::Source;
      int part = 0, p = 0;
      slotParamOf(index, &kind, &part, &p);
      const EngineInfo& info = slotEngineInfo(kind, engineFor(kind, engines));
      if (p < info.paramCount && info.params[p].format != nullptr) {
        info.params[p].format(v, buf, size);
      } else {
        std::snprintf(buf, size, "-");
      }
      break;
    }
    case ParamFormat::SourceEngine:
      std::snprintf(buf, size, "%s", sourceEngineInfo(sourceEngineFromSelector(v)).displayName);
      break;
    case ParamFormat::FilterEngine:
      std::snprintf(buf, size, "%s", filterEngineInfo(filterEngineFromSelector(v)).displayName);
      break;
    case ParamFormat::ShaperEngine:
      std::snprintf(buf, size, "%s", shaperEngineInfo(shaperEngineFromSelector(v)).displayName);
      break;
    case ParamFormat::InputSource:
      std::snprintf(buf, size, "%s", pick(kInputSources, 3, v));
      break;
    case ParamFormat::Percent:
      std::snprintf(buf, size, "%d%%", int(std::lround(v * 100.0f)));
      break;
    case ParamFormat::Pan:
      if (v < -0.005f) {
        std::snprintf(buf, size, "L%d", int(std::lround(-v * 100.0f)));
      } else if (v > 0.005f) {
        std::snprintf(buf, size, "R%d", int(std::lround(v * 100.0f)));
      } else {
        std::snprintf(buf, size, "C");
      }
      break;
    case ParamFormat::GlideTime: {
      const float seconds = glideSeconds(v);
      if (seconds <= 0.0f) {
        std::snprintf(buf, size, "Off");
      } else if (seconds < 1.0f) {
        std::snprintf(buf, size, "%d ms", int(std::lround(seconds * 1000.0f)));
      } else {
        std::snprintf(buf, size, "%.2f s", double(seconds));
      }
      break;
    }
    case ParamFormat::Semitones:
      std::snprintf(buf, size, "%+d st", int(v));
      break;
    case ParamFormat::BendRange:
      std::snprintf(buf, size, "+/-%d st", int(v));
      break;
    case ParamFormat::Note: {
      const int note = int(v);
      std::snprintf(buf, size, "%s%d", kNotes[note % 12], note / 12 - 1);
      break;
    }
    case ParamFormat::Channel:
      if (int(v) == 0) {
        std::snprintf(buf, size, "Omni");
      } else {
        std::snprintf(buf, size, "%d", int(v));
      }
      break;
    case ParamFormat::VoiceMode:
      std::snprintf(buf, size, "%s", pick(kVoiceModes, 3, v));
      break;
    case ParamFormat::VoiceCount:
      std::snprintf(buf, size, "%d", int(v));
      break;
    case ParamFormat::Trigger:
      std::snprintf(buf, size, "%s", pick(kTriggers, 2, v));
      break;
    case ParamFormat::OnOff:
      std::snprintf(buf, size, "%s", pick(kOnOff, 2, v));
      break;
    case ParamFormat::OutputBus:
      std::snprintf(buf, size, "%s", pick(kOutputs, 3, v));
      break;
    case ParamFormat::PartMode:
      std::snprintf(buf, size, "%s", pick(kModes, 4, v));
      break;
  }
}

const char* paramName(size_t index, int sourceEngine) {
  PartEngines engines;
  engines.source = sourceEngine;
  return paramName(index, engines);
}

const char* paramName(size_t index, const PartEngines& engines) {
  SlotKind kind = SlotKind::Source;
  int part = 0, p = 0;
  if (!slotParamOf(index, &kind, &part, &p)) return paramSpec(index).name;
  const EngineInfo& info = slotEngineInfo(kind, engineFor(kind, engines));
  return p < info.paramCount ? info.params[p].name : "(unused)";
}

}  // namespace eg13
