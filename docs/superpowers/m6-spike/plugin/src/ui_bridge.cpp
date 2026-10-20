#include "ui_bridge.h"

#include <cmath>
#include <cstdio>

#include "engine/engine_registry.h"
#include "params/param_ids.h"
#include "synth/param_table.h"

namespace eg13 {

// core_api.h mirrors the permanent parameter-ID scheme; keep them in step.
static_assert(kParamCount <= size_t(EG13_MAX_PARAMS), "EG13Snapshot.values is too small");
static_assert(kNumParts == EG13_NUM_PARTS, "part count");
static_assert(uint32_t(params::Slot::Part) == EG13_SLOT_PART, "slot ids");
static_assert(uint32_t(params::Slot::Source) == EG13_SLOT_SOURCE, "slot ids");
static_assert(uint32_t(params::Slot::Filter) == EG13_SLOT_FILTER, "slot ids");
static_assert(uint32_t(params::Slot::Shaper) == EG13_SLOT_SHAPER, "slot ids");
static_assert(uint32_t(params::Slot::PartFx) == EG13_SLOT_PART_FX, "slot ids");
static_assert(params::kMasterPart == EG13_MASTER_PART, "master part");
static_assert(uint32_t(PartParam::Level) == EG13_PART_LEVEL, "part params");
static_assert(uint32_t(PartParam::Pan) == EG13_PART_PAN, "part params");
static_assert(uint32_t(PartParam::Glide) == EG13_PART_GLIDE, "part params");
static_assert(uint32_t(PartParam::Transpose) == EG13_PART_TRANSPOSE, "part params");
static_assert(uint32_t(PartParam::BaseNote) == EG13_PART_BASE_NOTE, "part params");
static_assert(uint32_t(PartParam::MidiChannel) == EG13_PART_MIDI_CHANNEL, "part params");
static_assert(uint32_t(PartParam::KeyLow) == EG13_PART_KEY_LOW, "part params");
static_assert(uint32_t(PartParam::KeyHigh) == EG13_PART_KEY_HIGH, "part params");
static_assert(uint32_t(PartParam::VoiceMode) == EG13_PART_VOICE_MODE, "part params");
static_assert(uint32_t(PartParam::VoiceCount) == EG13_PART_VOICE_COUNT, "part params");
static_assert(uint32_t(PartParam::Trigger) == EG13_PART_TRIGGER, "part params");
static_assert(uint32_t(PartParam::BendRange) == EG13_PART_BEND_RANGE, "part params");
static_assert(uint32_t(PartParam::Mono) == EG13_PART_MONO, "part params");
static_assert(uint32_t(PartParam::Output) == EG13_PART_OUTPUT, "part params");
static_assert(uint32_t(MasterParam::Level) == EG13_MASTER_LEVEL, "master params");
static_assert(uint32_t(MasterParam::Dry) == EG13_MASTER_DRY, "master params");
static_assert(uint32_t(MasterParam::Mode) == EG13_MASTER_MODE, "master params");
static_assert(uint32_t(MasterParam::SplitNote) == EG13_MASTER_SPLIT_NOTE, "master params");

namespace {

UiBridge* bridge(void* context) { return static_cast<UiBridge*>(context); }

void pushParam(void* context, UiEvent::Type type, uint32_t id, double value) {
  UiEvent e;
  e.type = type;
  e.id = id;
  e.value = value;
  bridge(context)->push(e);
}

void pushNote(void* context, UiEvent::Type type, int32_t channel, int32_t key, double velocity) {
  if (channel < 0 || channel > 15 || key < 0 || key > 127) return;
  UiEvent e;
  e.type = type;
  e.channel = int8_t(channel);
  e.key = int16_t(key);
  e.value = velocity;
  bridge(context)->push(e);
}

void writeText(char* buffer, uint32_t size, const char* text) {
  if (buffer != nullptr && size > 0) std::snprintf(buffer, size, "%s", text);
}

}  // namespace

UiBridge::UiBridge(Synth& synth, UiHost host) : synth_(synth), host_(host) {
  api_.context = this;
  api_.param_count = [](void* c) { return bridge(c)->paramCount(); };
  api_.param_info = [](void* c, uint32_t i, EG13ParamInfo* info) { return bridge(c)->paramInfo(i, info); };
  api_.param_name = [](void* c, uint32_t i, char* b, uint32_t s) { return bridge(c)->paramName(i, b, s); };
  api_.param_text = [](void* c, uint32_t i, double v, char* b, uint32_t s) {
    return bridge(c)->paramText(i, v, b, s);
  };
  api_.engine_count = [](void* c, uint32_t slot) { return bridge(c)->engineCount(slot); };
  api_.engine_name = [](void* c, uint32_t slot, uint32_t e, char* b, uint32_t s) {
    return bridge(c)->engineName(slot, e, b, s);
  };
  api_.engine_id = [](void* c, uint32_t slot, uint32_t e, char* b, uint32_t s) {
    return bridge(c)->engineId(slot, e, b, s);
  };
  api_.begin_edit = [](void* c, uint32_t id) { pushParam(c, UiEvent::Type::BeginEdit, id, 0.0); };
  api_.edit = [](void* c, uint32_t id, double v) { pushParam(c, UiEvent::Type::Edit, id, v); };
  api_.end_edit = [](void* c, uint32_t id) { pushParam(c, UiEvent::Type::EndEdit, id, 0.0); };
  api_.note_on = [](void* c, int32_t ch, int32_t key, double v) {
    pushNote(c, UiEvent::Type::NoteOn, ch, key, v);
  };
  api_.note_off = [](void* c, int32_t ch, int32_t key) { pushNote(c, UiEvent::Type::NoteOff, ch, key, 0.0); };
  api_.snapshot = [](void* c, EG13Snapshot* out) { bridge(c)->snapshot(out); };
  api_.request_zoom = [](void* c, double zoom) { bridge(c)->requestZoom(zoom); };
}

void UiBridge::push(const UiEvent& event) {
  pump();
  if (!overflow_.empty() || !queue_.push(event)) overflow_.push_back(event);
  if (host_.requestFlush != nullptr) host_.requestFlush(host_.context);
}

void UiBridge::pump() {
  while (!overflow_.empty() && queue_.push(overflow_.front())) overflow_.pop_front();
}

void UiBridge::drain(const clap_output_events_t* out, bool withNotes) {
  UiEvent e;
  while (queue_.pop(e)) {
    switch (e.type) {
      case UiEvent::Type::NoteOn:
        if (withNotes) synth_.noteOn(e.channel, e.key, float(e.value));
        break;
      case UiEvent::Type::NoteOff:
        synth_.noteOff(e.channel, e.key);
        break;
      case UiEvent::Type::BeginEdit:
      case UiEvent::Type::EndEdit: {
        if (paramIndex(e.id) < 0 || out == nullptr) break;
        clap_event_param_gesture_t ev{};
        ev.header.size = sizeof(ev);
        ev.header.time = 0;
        ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        ev.header.type = e.type == UiEvent::Type::BeginEdit ? CLAP_EVENT_PARAM_GESTURE_BEGIN
                                                            : CLAP_EVENT_PARAM_GESTURE_END;
        ev.header.flags = 0;
        ev.param_id = e.id;
        out->try_push(out, &ev.header);
        break;
      }
      case UiEvent::Type::Edit: {
        const int index = paramIndex(e.id);
        if (index < 0 || std::isnan(e.value)) break;
        const float value = clampParam(size_t(index), e.value);
        synth_.setParam(e.id, double(value));
        if (out == nullptr) break;
        clap_event_param_value_t ev{};
        ev.header.size = sizeof(ev);
        ev.header.time = 0;
        ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        ev.header.type = CLAP_EVENT_PARAM_VALUE;
        ev.header.flags = 0;
        ev.param_id = e.id;
        ev.cookie = nullptr;
        ev.note_id = -1;
        ev.port_index = -1;
        ev.channel = -1;
        ev.key = -1;
        ev.value = double(value);
        out->try_push(out, &ev.header);
        break;
      }
    }
  }
}

uint32_t UiBridge::paramCount() const { return uint32_t(kParamCount); }

bool UiBridge::paramInfo(uint32_t index, EG13ParamInfo* info) const {
  if (index >= kParamCount || info == nullptr) return false;
  const ParamSpec& spec = paramSpec(index);
  info->id = spec.id;
  info->flags = spec.stepped ? uint32_t(EG13_PARAM_STEPPED) : 0u;
  info->min_value = double(spec.minValue);
  info->max_value = double(spec.maxValue);
  info->default_value = double(paramDefault(index, synth_.kind()));
  return true;
}

bool UiBridge::paramName(uint32_t index, char* buffer, uint32_t size) const {
  if (index >= kParamCount) return false;
  writeText(buffer, size, paramSpec(index).name);
  return true;
}

bool UiBridge::paramText(uint32_t index, double value, char* buffer, uint32_t size) const {
  if (index >= kParamCount || buffer == nullptr || size == 0) return false;
  formatParam(index, value, buffer, size);
  return true;
}

uint32_t UiBridge::engineCount(uint32_t slot) const {
  // Until M2 merges, the Source slot plays only the Macro Oscillator.
  return slot == EG13_SLOT_SOURCE ? 1u : 0u;
}

bool UiBridge::engineName(uint32_t slot, uint32_t engine, char* buffer, uint32_t size) const {
  if (engine >= engineCount(slot)) return false;
  writeText(buffer, size, sourceEngineInfo(int(engine)).displayName);
  return true;
}

bool UiBridge::engineId(uint32_t slot, uint32_t engine, char* buffer, uint32_t size) const {
  if (engine >= engineCount(slot)) return false;
  writeText(buffer, size, sourceEngineInfo(int(engine)).id);
  return true;
}

void UiBridge::snapshot(EG13Snapshot* out) {
  pump();
  if (out == nullptr) return;
  out->param_count = uint32_t(kParamCount);
  for (size_t i = 0; i < kParamCount; ++i) out->values[i] = synth_.param(paramSpec(i).id);
  for (int part = 0; part < kNumParts; ++part) {
    synth_.takePartPeaks(part, &out->part_peak[part][0], &out->part_peak[part][1]);
  }
}

void UiBridge::requestZoom(double zoom) {
  if (host_.requestZoom != nullptr) host_.requestZoom(host_.context, zoom);
}

}  // namespace eg13
