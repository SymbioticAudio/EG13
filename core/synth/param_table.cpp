#include "synth/param_table.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include "engine/audio_input_engine.h"
#include "engine/engine_registry.h"
#include "engine/plaits_engine.h"
#include "params/param_ids.h"

namespace eg13 {

static_assert(kSourceParamCount <= size_t(kMaxSlotParams), "ParamValues must hold every Source P");

namespace {

const char* const kSourceModules[kNumParts] = {"Part A/Source", "Part B/Source"};
const char* const kPartModules[kNumParts] = {"Part A", "Part B"};

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

const std::array<ParamSpec, kParamCount>& table() {
  static const std::array<ParamSpec, kParamCount> specs = [] {
    std::array<ParamSpec, kParamCount> t{};
    const EngineInfo& info = PlaitsEngine::staticInfo();
    const EngineInfo& fxInfo = AudioInputEngine::staticInfo();
    for (int part = 0; part < kNumParts; ++part) {
      t[sourceEngineParamIndex(part)] = {params::id(uint32_t(part), params::Slot::Source, 0), "Engine",
                                         kSourceModules[part], 0.0f, float(kSourceEngineSelectorMax),
                                         0.0f, part == 0 ? 4.0f : 0.0f, true,
                                         ParamFormat::SourceEngine};
      for (int p = 0; p < int(kSourceParamCount); ++p) {
        const bool used = p < info.paramCount;
        const float def = used ? info.params[p].defaultValue : 0.0f;
        float effectDef = def;
        if (part == 0) {  // EG13 FX: Part A plays the Audio Input engine
          effectDef = p < fxInfo.paramCount ? fxInfo.params[p].defaultValue : 0.0f;
        }
        t[sourceParamIndex(part, p)] = {params::id(uint32_t(part), params::Slot::Source, uint32_t(p + 1)),
                                        used ? info.params[p].name : "(unused)", kSourceModules[part],
                                        0.0f, 1.0f, def,
                                        effectDef, false, ParamFormat::Engine};
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

// Index of a Source P (0..kSourceParamCount-1) for a table index, or -1.
int sourceParamOf(size_t index) {
  if (index >= size_t(kNumParts) * kParamsPerPart) return -1;
  const int local = int(index % kParamsPerPart) - 1;
  return local >= 0 && local < int(kSourceParamCount) ? local : -1;
}

}  // namespace

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
      const EngineInfo& info = sourceEngineInfo(sourceEngine);
      const int p = sourceParamOf(index);
      if (p >= 0 && p < info.paramCount) {
        info.params[p].format(v, buf, size);
      } else {
        std::snprintf(buf, size, "-");
      }
      break;
    }
    case ParamFormat::SourceEngine:
      std::snprintf(buf, size, "%s", sourceEngineInfo(sourceEngineFromSelector(v)).displayName);
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
  const int p = sourceParamOf(index);
  if (p < 0) return paramSpec(index).name;
  const EngineInfo& info = sourceEngineInfo(sourceEngine);
  return p < info.paramCount ? info.params[p].name : "(unused)";
}

}  // namespace eg13
