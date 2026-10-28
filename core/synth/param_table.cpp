#include "synth/param_table.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include "engine/engine_registry.h"
#include "engine/shelves_engine.h"
#include "engine/sst_filter_engine.h"
#include "engine/tides_engine.h"
#include "engine/warps_engine.h"
#include "params/param_ids.h"

namespace eg13 {

static_assert(kSourceParamCount <= size_t(kMaxSlotParams), "ParamValues must hold every Source P");
static_assert(kShaperParamCount <= size_t(kMaxSlotParams), "ParamValues must hold every Shaper P");
static_assert(kModSlotParamCount <= size_t(kMaxSlotParams), "ParamValues must hold every mod slot P");
static_assert(kPartFxSlotParamCount <= size_t(kMaxSlotParams), "ParamValues must hold every Part FX P");

namespace {

const char* const kSlotModules[kNumSlotKinds][kNumParts] = {
    {"Part A/Source", "Part B/Source"}, {"Part A/Filter", "Part B/Filter"}, {"Part A/Shaper", "Part B/Shaper"},
    {"Part A/Mod 1", "Part B/Mod 1"},   {"Part A/Mod 2", "Part B/Mod 2"},   {"Part A/Mod 3", "Part B/Mod 3"},
    {"Part A/Mod 4", "Part B/Mod 4"},   {"Part A/Part FX", "Part B/Part FX"},
};
const char* const kPartModules[kNumParts] = {"Part A", "Part B"};
const char* const kMatrixModules[kNumParts] = {"Part A/Matrix", "Part B/Matrix"};
const char* const kRouteNames[kNumRoutes] = {"Route 1", "Route 2", "Route 3",  "Route 4",  "Route 5",  "Route 6",
                                             "Route 7", "Route 8", "Route 9", "Route 10", "Route 11", "Route 12"};

struct PartParamDef {
  PartParam param;
  const char* name;
  float minValue;
  float maxValue;
  float defaultValue;
  bool stepped;
  ParamFormat format;
};

// Stepped settings are integer plain values: a value keeps its meaning when an option is added at
// the end of a list (the maximum grows, nothing is renumbered).
const PartParamDef kPartDefs[kPartParamCount] = {
    // Task 8g: the knob position 0..1, silence to +12 dB, 0 dB at the default (part/part_level.h).
    {PartParam::Level, "Level", 0, 1, kPartLevelUnity, false, ParamFormat::PartLevel},
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
    // M4. MPE: Off, Lower zone, Upper zone (append-only). CC A-D: provisional ruling 14.
    {PartParam::MpeMode, "MPE", 0, 2, 0, true, ParamFormat::MpeMode},
    {PartParam::MpeBendRange, "MPE Bend Range", 0, 96, 48, true, ParamFormat::BendRange},
    {PartParam::CcA, "CC A", 0, 127, 2, true, ParamFormat::CcNumber},
    {PartParam::CcB, "CC B", 0, 127, 4, true, ParamFormat::CcNumber},
    {PartParam::CcC, "CC C", 0, 127, 11, true, ParamFormat::CcNumber},
    {PartParam::CcD, "CC D", 0, 127, 16, true, ParamFormat::CcNumber},
    // M5. Part FX Input: Part mix, Input, Part mix + Input (append-only: the maximum grows).
    {PartParam::PartFxInput, "Part FX Input", 0, 2, 0, true, ParamFormat::PartFxInput},
};

params::Slot idSlot(SlotKind kind) {
  return kind == SlotKind::Source   ? params::Slot::Source
         : kind == SlotKind::Filter ? params::Slot::Filter
         : kind == SlotKind::Shaper ? params::Slot::Shaper
         : kind == SlotKind::PartFx ? params::Slot::PartFx
                                    : params::Slot(uint32_t(params::Slot::Mod1) + uint32_t(modSlotIndex(kind)));
}

// Default engine per slot, part and plugin: the init patches (spec §6). EG13 FX's Part A is
// Audio Input -> Analog Filter with an LFO in Mod 1 and Granular in Part FX; the instrument has the State Variable Filter
// (provisional ruling 28), envelopes in Mod 1 and 2 and an LFO in Mod 3. Everything else starts on
// the slot's first engine (Macro Oscillator, Off).
int defaultEngine(SlotKind kind, int part, PluginKind plugin) {
  if (plugin == PluginKind::Effect) {
    if (part != 0) return 0;
    if (kind == SlotKind::Source) return int(SourceEngine::AudioInput);
    if (kind == SlotKind::Filter) return int(FilterEngine::Analog);
    if (kind == SlotKind::Mod1) return int(ModEngineKind::Lfo);
    if (kind == SlotKind::PartFx) return int(PartFxEngine::Granular);
    return 0;
  }
  if (kind == SlotKind::Filter) return int(FilterEngine::StateVariable);
  if (kind == SlotKind::Mod1 || kind == SlotKind::Mod2) return int(ModEngineKind::Envelope);
  if (kind == SlotKind::Mod3) return int(ModEngineKind::Lfo);
  return 0;
}

// The instrument init patch's cutoff, 70 % (20 Hz * 1000^0.7, about 2.5 kHz): Mod 2's +30 %
// opens it fully at the envelope's peak (spec §6; provisional ruling 28).
constexpr float kInstrumentCutoff = 0.7f;

// Host defaults of the Filter and Shaper P's (M3 final review I1; controller ruling, provisional).
// CLAP reports one default per parameter and cannot change it while active, so a host's
// reset-to-default sets the same value whatever engine is selected. Each value is chosen so
// that a slot reset as a whole gives a sensible, audible result on every engine of the slot
// (tests/test_levels.cpp, "Host defaults ..."). Engine switching still loads each engine's own
// stash and EngineInfo defaults; these values are only what the host shows as "default" and
// what a fresh project holds in a slot's P's.
//
// Filter: every filter's own defaults where they agree. P1 fully open (all three filters
// default to it; the instrument's init patch uses kInstrumentCutoff instead), P2 no resonance
// (the Analog Filter's and Classic Filters' default; the SVF reads it as Q 0.5, flat with a soft
// knee, rather than its own 0.707), P3 the first option (Analog Filter LP4, Classic Filters type
// 0 = 12 dB low-pass, SVF LP), P4 Character centred.
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
// Mod slots (provisional ruling 1): one value per P position that suits every mod engine. Each
// engine places its controls so these are its own defaults: stepped lists default to their first
// option (0), times and levels as noted in each engine's header.
//   Envelope: Mode ADSR, Attack 1 ms, shapes linear, Decay and Release 0.5 s, Sustain 100 %.
//   LFO: Cycle, Sync off, 2 Hz, neutral shape/slope/smoothness, Shift 100 %, Phase outputs, Medium.
//   Random / S&H: VCV's defaults (coin toss, rate and biases centred, no deja vu or jitter), X Range
//   Full (its last option, at 100 %).
//   Function Generator: Envelope, fastest attack, 0.4 s decay and release, full sustain, 2.2 Hz
//   LFO, sine.
const float kModHostDefaults[kModSlotParamCount] = {0.0f, 0.0f, 0.5f, 0.5f, 0.5f, 1.0f, 0.5f, 0.5f,
                                                    0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

float modHostDefault(PluginKind plugin, int part, int slot, int p) {
  // Init patch LFO rates (provisional ruling 29): the instrument's Mod 3 vibrato at about 5 Hz,
  // EG13 FX's Mod 1 filter sweep at about 0.25 Hz. Every mod engine accepts any value here.
  if (p == 2 && plugin == PluginKind::Instrument && slot == 2) return TidesEngine::frequencyFor(5.0, TidesEngine::Medium);
  if (p == 2 && plugin == PluginKind::Effect && part == 0 && slot == 0) {
    return TidesEngine::frequencyFor(0.25, TidesEngine::Medium);
  }
  return kModHostDefaults[p];
}

float hostDefault(SlotKind kind, int part, int p, const EngineInfo& engine, PluginKind plugin) {
  if (kind == SlotKind::Filter) {
    return p == 0 && plugin == PluginKind::Instrument ? kInstrumentCutoff : kFilterHostDefaults[p];
  }
  if (kind == SlotKind::Shaper) return kShaperHostDefaults[p];
  if (kind == SlotKind::PartFx) return kPartFxHostDefaults[p];  // the same for every Part FX type
  if (isModSlot(kind)) return modHostDefault(plugin, part, modSlotIndex(kind), p);
  return p < engine.paramCount ? engine.params[p].defaultValue : 0.0f;  // Source: M2's rule
}

// A per-part LFO in EG13 FX's Mod 1; every other slot per voice (provisional ruling 2).
float scopeDefault(PluginKind plugin, int part, int slot) {
  return plugin == PluginKind::Effect && part == 0 && slot == 0 ? 1.0f : 0.0f;
}

int engineFor(SlotKind kind, const PartEngines& e) {
  if (isModSlot(kind)) return e.mod[modSlotIndex(kind)];
  if (kind == SlotKind::PartFx) return e.partFx;
  return kind == SlotKind::Source ? e.source : kind == SlotKind::Filter ? e.filter : e.shaper;
}

ParamFormat selectorFormat(SlotKind kind) {
  return kind == SlotKind::Source   ? ParamFormat::SourceEngine
         : kind == SlotKind::Filter ? ParamFormat::FilterEngine
         : kind == SlotKind::Shaper ? ParamFormat::ShaperEngine
         : kind == SlotKind::PartFx ? ParamFormat::PartFxEngine
                                    : ParamFormat::ModEngine;
}

float selectorMax(SlotKind kind) {
  return float(kind == SlotKind::Source   ? kSourceEngineSelectorMax
               : kind == SlotKind::Filter ? kFilterEngineSelectorMax
               : kind == SlotKind::Shaper ? kShaperEngineSelectorMax
               : kind == SlotKind::PartFx ? kPartFxEngineSelectorMax
                                          : kModEngineSelectorMax);
}

const std::array<ParamSpec, kParamCount>& table() {
  static const std::array<ParamSpec, kParamCount> specs = [] {
    std::array<ParamSpec, kParamCount> t{};
    for (int part = 0; part < kNumParts; ++part) {
      for (int k = 0; k < kNumSlotKinds; ++k) {
        const SlotKind kind = SlotKind(k);
        const int instrument = defaultEngine(kind, part, PluginKind::Instrument);
        const int effect = defaultEngine(kind, part, PluginKind::Effect);
        const char* module = kSlotModules[k][part];
        // Engine selectors: a fixed range of 0..31 (unknown values select the slot's first
        // engine), so appending engines never changes the range or what a value means.
        // RULE (Part FX Type): 0 Off, 1 Granular, 2 Granular Delay, 3 Resonator, 4 Reverb; new
        // engines are appended, 0..4 keep their meaning, and a value with no engine reads as Off.
        t[slotEngineParamIndex(kind, part)] = {params::id(uint32_t(part), idSlot(kind), 0),
                                               kind == SlotKind::Source || isModSlot(kind) ? "Engine" : "Type",
                                               module, 0.0f, selectorMax(kind), float(instrument), float(effect),
                                               true, selectorFormat(kind)};
        const EngineInfo& inst = slotEngineInfo(kind, instrument);
        const EngineInfo& fx = slotEngineInfo(kind, effect);
        for (int p = 0; p < int(slotParamCount(kind)); ++p) {
          const bool used = p < inst.paramCount;
          t[slotParamIndex(kind, part, p)] = {params::id(uint32_t(part), idSlot(kind), uint32_t(p + 1)),
                                              used ? inst.params[p].name : "(unused)", module, 0.0f, 1.0f,
                                              hostDefault(kind, part, p, inst, PluginKind::Instrument),
                                              hostDefault(kind, part, p, fx, PluginKind::Effect), false,
                                              ParamFormat::Engine};
        }
        if (isModSlot(kind)) {
          // Scope: 0 per voice, 1 per part. RULE: a new scope is appended as 2 (max grows).
          const int slot = modSlotIndex(kind);
          t[modScopeParamIndex(part, slot)] = {params::id(uint32_t(part), idSlot(kind), 1 + uint32_t(kModSlotParamCount)),
                                               "Scope", module, 0.0f, 1.0f,
                                               scopeDefault(PluginKind::Instrument, part, slot),
                                               scopeDefault(PluginKind::Effect, part, slot), true,
                                               ParamFormat::ModScope};
        }
      }
      for (int r = 0; r < kNumRoutes; ++r) {
        t[routeAmountParamIndex(part, r)] = {params::id(uint32_t(part), params::Slot::Matrix, uint32_t(r + 1)),
                                             kRouteNames[r], kMatrixModules[part], -1.0f, 1.0f,
                                             initPatchAmount(PluginKind::Instrument, part, r),
                                             initPatchAmount(PluginKind::Effect, part, r), false,
                                             ParamFormat::RouteAmount};
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
    // Voice Gain: 0 = -12 dB (instrument), 1 = 0 dB (EG13 FX). RULE: new options are appended
    // (max grows). Provisional ruling 24.
    t[masterParamIndex(MasterParam::VoiceGain)] = {master(MasterParam::VoiceGain), "Voice Gain", "Master", 0, 1,
                                                   0.0f, 1.0f, true, ParamFormat::VoiceGain};
    // Soft Clip: 0 Off, 1 On (provisional ruling 23). RULE: new options (e.g. other curves) are
    // appended (max grows); 0 and 1 keep their meaning.
    t[masterParamIndex(MasterParam::SoftClip)] = {master(MasterParam::SoftClip), "Soft Clip", "Master", 0, 1,
                                                  1.0f, 1.0f, true, ParamFormat::OnOff};
    // Latency Mode: ConverterQuality, 0 Live, 1 Studio (M5 plan rulings 4, 5: the instrument
    // starts in Live, EG13 FX in Studio). Not automatable (the plugin); applies at the next
    // activate. RULE: new options are appended (max grows); 0 and 1 keep their meaning.
    t[masterParamIndex(MasterParam::LatencyMode)] = {master(MasterParam::LatencyMode), "Latency Mode", "Master", 0, 1,
                                                     float(ConverterQuality::Live), float(ConverterQuality::Studio),
                                                     true, ParamFormat::LatencyMode};
    return t;
  }();
  return specs;
}

const char* pick(const char* const* names, int count, float value) {
  const int i = std::clamp(int(std::lround(value)), 0, count - 1);
  return names[i];
}

}  // namespace

Route initPatchRoute(PluginKind plugin, int part, int route) {
  if (plugin == PluginKind::Effect) {
    // Mod 1's LFO (per part) -> Filter P1 (spec §6).
    if (part == 0 && route == 0) return {modOutputSource(0, 0), uint16_t(kDestFilterP + 0), kSrcNone};
    return {};
  }
  switch (route) {  // both parts (spec §6)
    case 0: return {modOutputSource(0, 0), kDestAmp, kSrcVelocity};       // Mod 1 -> Amp, via velocity
    case 1: return {modOutputSource(1, 0), uint16_t(kDestFilterP + 0), kSrcNone};  // Mod 2 -> Filter P1
    case 2: return {modOutputSource(2, 0), kDestPitch, kSrcModWheel};     // Mod 3 -> pitch, via mod wheel
    default: return {};
  }
}

float initPatchAmount(PluginKind plugin, int part, int route) {
  if (plugin == PluginKind::Effect) return part == 0 && route == 0 ? 0.2f : 0.0f;  // +20 %
  switch (route) {
    case 0: return 1.0f;                          // 100 %
    case 1: return 0.3f;                          // +30 %
    case 2: return 0.5f / kPitchRangeSemitones;   // +-0.5 semitone
    default: return 0.0f;
  }
}

int slotEngineCount(SlotKind kind) {
  return kind == SlotKind::Source   ? kNumSourceEngines
         : kind == SlotKind::Filter ? kNumFilterEngines
         : kind == SlotKind::Shaper ? kNumShaperEngines
         : kind == SlotKind::PartFx ? kNumPartFxEngines
                                    : kNumModEngines;
}

int slotEngineFromSelector(SlotKind kind, double value) {
  return kind == SlotKind::Source   ? sourceEngineFromSelector(value)
         : kind == SlotKind::Filter ? filterEngineFromSelector(value)
         : kind == SlotKind::Shaper ? shaperEngineFromSelector(value)
         : kind == SlotKind::PartFx ? partFxEngineFromSelector(value)
                                    : modEngineFromSelector(value);
}

const EngineInfo& slotEngineInfo(SlotKind kind, int engine) {
  return kind == SlotKind::Source   ? sourceEngineInfo(engine)
         : kind == SlotKind::Filter ? filterEngineInfo(engine)
         : kind == SlotKind::Shaper ? shaperEngineInfo(engine)
         : kind == SlotKind::PartFx ? partFxEngineInfo(engine)
                                    : modEngineInfo(engine).engine;
}

bool slotParamOf(size_t index, SlotKind* kind, int* part, int* p) {
  if (index >= size_t(kNumParts) * kParamsPerPart) return false;
  const size_t local = index % kParamsPerPart;
  for (int k = 0; k < kNumSlotKinds; ++k) {
    const size_t first = slotOffset(SlotKind(k)) + 1;
    if (local >= first && local < first + slotParamCount(SlotKind(k))) {
      *kind = SlotKind(k);
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
  for (int k = 0; k < kNumSlotKinds; ++k) {
    if (local == slotOffset(SlotKind(k))) {
      *kind = SlotKind(k);
      *part = int(index / kParamsPerPart);
      return true;
    }
  }
  return false;
}

bool modScopeOf(size_t index, int* part, int* slot) {
  if (index >= size_t(kNumParts) * kParamsPerPart) return false;
  for (int s = 0; s < kNumModSlots; ++s) {
    if (index % kParamsPerPart == modScopeParamIndex(0, s)) {
      *part = int(index / kParamsPerPart);
      *slot = s;
      return true;
    }
  }
  return false;
}

bool routeAmountOf(size_t index, int* part, int* route) {
  if (index >= size_t(kNumParts) * kParamsPerPart) return false;
  const size_t local = index % kParamsPerPart;
  if (local < kSlotsBlockSize || local >= kSlotsBlockSize + size_t(kNumRoutes)) return false;
  *part = int(index / kParamsPerPart);
  *route = int(local - kSlotsBlockSize);
  return true;
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
  static const char* const kScopes[2] = {"Per voice", "Per part"};
  static const char* const kMpeModes[3] = {"Off", "Lower zone", "Upper zone"};
  static const char* const kVoiceGains[2] = {"-12 dB", "0 dB"};
  static const char* const kLatencyModes[2] = {"Live", "Studio"};  // ConverterQuality order
  static const char* const kPartFxInputs[3] = {"Part mix", "Input", "Part mix + Input"};  // PartFxInput order

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
    case ParamFormat::ModEngine:
      std::snprintf(buf, size, "%s", modEngineInfo(modEngineFromSelector(v)).engine.displayName);
      break;
    case ParamFormat::PartFxEngine:
      std::snprintf(buf, size, "%s", partFxEngineInfo(partFxEngineFromSelector(v)).displayName);
      break;
    case ParamFormat::PartFxInput:
      std::snprintf(buf, size, "%s", pick(kPartFxInputs, 3, v));
      break;
    case ParamFormat::ModScope:
      std::snprintf(buf, size, "%s", pick(kScopes, 2, v));
      break;
    case ParamFormat::RouteAmount: {
      int part = 0, route = 0;
      routeAmountOf(index, &part, &route);
      const uint16_t destination = engines.routeDestination[route];
      if (destinationKind(destination) == DestinationKind::None) {
        std::snprintf(buf, size, "-");
      } else if (destination == kDestPitch) {
        std::snprintf(buf, size, "%+.2f st", double(v * kPitchRangeSemitones));
      } else {
        std::snprintf(buf, size, "%+d%%", int(std::lround(v * 100.0f)));
      }
      break;
    }
    case ParamFormat::MpeMode:
      std::snprintf(buf, size, "%s", pick(kMpeModes, 3, v));
      break;
    case ParamFormat::CcNumber:
      std::snprintf(buf, size, "CC %d", int(v));
      break;
    case ParamFormat::VoiceGain:
      std::snprintf(buf, size, "%s", pick(kVoiceGains, 2, v));
      break;
    case ParamFormat::LatencyMode:
      std::snprintf(buf, size, "%s", pick(kLatencyModes, 2, v));
      break;
    case ParamFormat::InputSource:
      std::snprintf(buf, size, "%s", pick(kInputSources, 3, v));
      break;
    case ParamFormat::Percent:
      std::snprintf(buf, size, "%d%%", int(std::lround(v * 100.0f)));
      break;
    case ParamFormat::PartLevel: {
      const float gain = partLevelGain(v);
      const double db = gain > 0.0f ? 20.0 * std::log10(double(gain)) : 0.0;
      if (gain <= 0.0f) {
        std::snprintf(buf, size, "-inf dB");
      } else if (std::fabs(db) < 0.05) {
        std::snprintf(buf, size, "0.0 dB");  // never "-0.0 dB" or "+0.0 dB"
      } else {
        std::snprintf(buf, size, "%+.1f dB", db);
      }
      break;
    }
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
