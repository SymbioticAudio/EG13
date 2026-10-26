#include "core_api.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "params/param_ids.h"
#include "synth/param_table.h"
#include "synth/ui_snapshot.h"

namespace eg13 {

// The C header mirrors these enums by value; a mismatch is a build error.
static_assert(EG13_MAX_PARAMS == kParamCount, "EG13_MAX_PARAMS must equal eg13::kParamCount");
static_assert(EG13_NUM_PARTS == kNumParts, "EG13_NUM_PARTS must equal eg13::kNumParts");
static_assert(EG13_MASTER_PART == params::kMasterPart, "master part");

static_assert(EG13_SLOT_PART == uint32_t(params::Slot::Part), "EG13Slot");
static_assert(EG13_SLOT_SOURCE == uint32_t(params::Slot::Source), "EG13Slot");
static_assert(EG13_SLOT_FILTER == uint32_t(params::Slot::Filter), "EG13Slot");
static_assert(EG13_SLOT_SHAPER == uint32_t(params::Slot::Shaper), "EG13Slot");
static_assert(EG13_SLOT_PART_FX == uint32_t(params::Slot::PartFx), "EG13Slot");
static_assert(EG13_SLOT_MOD1 == uint32_t(params::Slot::Mod1), "EG13Slot");
static_assert(EG13_SLOT_MOD2 == uint32_t(params::Slot::Mod2), "EG13Slot");
static_assert(EG13_SLOT_MOD3 == uint32_t(params::Slot::Mod3), "EG13Slot");
static_assert(EG13_SLOT_MOD4 == uint32_t(params::Slot::Mod4), "EG13Slot");
static_assert(EG13_SLOT_MATRIX == uint32_t(params::Slot::Matrix), "EG13Slot");

static_assert(EG13_SLOT_KIND_SOURCE == int(SlotKind::Source), "EG13SlotKind");
static_assert(EG13_SLOT_KIND_FILTER == int(SlotKind::Filter), "EG13SlotKind");
static_assert(EG13_SLOT_KIND_SHAPER == int(SlotKind::Shaper), "EG13SlotKind");
static_assert(EG13_SLOT_KIND_MOD1 == int(SlotKind::Mod1), "EG13SlotKind");
static_assert(EG13_SLOT_KIND_MOD2 == int(SlotKind::Mod2), "EG13SlotKind");
static_assert(EG13_SLOT_KIND_MOD3 == int(SlotKind::Mod3), "EG13SlotKind");
static_assert(EG13_SLOT_KIND_MOD4 == int(SlotKind::Mod4), "EG13SlotKind");
static_assert(EG13_SLOT_KIND_PART_FX == int(SlotKind::PartFx), "EG13SlotKind");
static_assert(EG13_SLOT_KIND_PART_FX + 1 == kNumSlotKinds, "EG13SlotKind covers every slot");

namespace {

CoreApiContext& ctx(void* context) { return *static_cast<CoreApiContext*>(context); }

bool validKind(uint32_t kind) { return kind < uint32_t(kNumSlotKinds); }
bool validEngine(uint32_t kind, uint32_t engine) {
  return validKind(kind) && engine < uint32_t(slotEngineCount(SlotKind(kind)));
}

bool copyText(const char* text, char* buffer, uint32_t size) {
  if (!buffer || size == 0) return false;
  std::snprintf(buffer, size, "%s", text ? text : "");
  return true;
}

uint32_t paramCount(void*) noexcept { return uint32_t(kParamCount); }

bool paramInfo(void*, uint32_t index, EG13ParamInfo* info) noexcept {
  if (!info || index >= kParamCount) return false;
  const ParamSpec& spec = paramSpec(index);
  info->id = spec.id;
  info->flags = spec.stepped ? EG13_PARAM_STEPPED : 0u;
  info->min = double(spec.minValue);
  info->max = double(spec.maxValue);
  info->def = double(spec.defaultValue);
  return true;
}

bool paramName(void* context, uint32_t index, char* buffer, uint32_t size) noexcept {
  if (index >= kParamCount) return false;
  return copyText(ctx(context).synth.paramName(index), buffer, size);
}

bool paramText(void* context, uint32_t index, double value, char* buffer, uint32_t size) noexcept {
  if (index >= kParamCount || !buffer || size == 0) return false;
  ctx(context).synth.formatParam(index, value, buffer, size);
  buffer[size - 1] = '\0';
  return true;
}

int32_t paramIndexOf(void*, uint32_t id) noexcept { return int32_t(paramIndex(id)); }

uint32_t engineCount(void*, uint32_t kind) noexcept {
  return validKind(kind) ? uint32_t(slotEngineCount(SlotKind(kind))) : 0u;
}

bool engineName(void*, uint32_t kind, uint32_t engine, char* buffer, uint32_t size) noexcept {
  if (!validEngine(kind, engine)) return false;
  return copyText(slotEngineInfo(SlotKind(kind), int(engine)).displayName, buffer, size);
}

uint32_t engineParamCount(void*, uint32_t kind, uint32_t engine) noexcept {
  if (!validEngine(kind, engine)) return 0;
  return uint32_t(slotEngineInfo(SlotKind(kind), int(engine)).paramCount);
}

uint32_t currentEngine(void* context, uint32_t kind, uint32_t part) noexcept {
  if (!validKind(kind) || part >= uint32_t(kNumParts)) return 0;
  return uint32_t(ctx(context).synth.engine(SlotKind(kind), int(part)));
}

void beginEdit(void* context, uint32_t id) noexcept { ctx(context).bridge.beginEdit(id); }
void edit(void* context, uint32_t id, double value) noexcept { ctx(context).bridge.edit(id, value); }
void endEdit(void* context, uint32_t id) noexcept { ctx(context).bridge.endEdit(id); }

void snapshot(void* context, EG13Snapshot* out) noexcept {
  if (!out) return;
  CoreApiContext& c = ctx(context);
  c.bridge.pump();  // edits that did not fit the queue
  UiSnapshot snap;
  c.synth.uiSnapshot(&snap);
  out->param_count = uint32_t(kParamCount);
  for (size_t i = 0; i < kParamCount; ++i) out->values[i] = double(snap.values[i]);
  for (int p = 0; p < kNumParts; ++p) {
    out->part_peak[p][0] = snap.partPeak[p][0];
    out->part_peak[p][1] = snap.partPeak[p][1];
    out->active_voices[p] = int32_t(snap.activeVoices[p]);
  }
  out->engine_generation = snap.engineGeneration;
}

void requestResize(void* context, uint32_t width, uint32_t height) noexcept {
  CoreApiContext& c = ctx(context);
  if (c.requestResize) c.requestResize(width, height);
}

}  // namespace

EG13CoreAPI makeCoreApi(CoreApiContext& context) {
  EG13CoreAPI api{};
  api.context = &context;
  api.param_count = paramCount;
  api.param_info = paramInfo;
  api.param_name = paramName;
  api.param_text = paramText;
  api.param_index = paramIndexOf;
  api.engine_count = engineCount;
  api.engine_name = engineName;
  api.engine_param_count = engineParamCount;
  api.current_engine = currentEngine;
  api.begin_edit = beginEdit;
  api.edit = edit;
  api.end_edit = endEdit;
  api.snapshot = snapshot;
  api.request_resize = requestResize;
  return api;
}

}  // namespace eg13
