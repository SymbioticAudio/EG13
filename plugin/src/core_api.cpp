#include "core_api.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "eg13_version.h"
#include "engine/engine_registry.h"
#include "mod/mod_routes.h"
#include "params/param_ids.h"
#include "synth/param_table.h"
#include "synth/ui_snapshot.h"

namespace eg13 {

// The C header mirrors these enums by value; a mismatch is a build error.
static_assert(EG13_MAX_PARAMS == kParamCount, "EG13_MAX_PARAMS must equal eg13::kParamCount");
static_assert(EG13_NUM_PARTS == kNumParts, "EG13_NUM_PARTS must equal eg13::kNumParts");
static_assert(EG13_NUM_ROUTES == kNumRoutes, "EG13_NUM_ROUTES must equal eg13::kNumRoutes");
static_assert(EG13_NUM_MOD_SLOTS == kNumModSlots, "EG13_NUM_MOD_SLOTS must equal eg13::kNumModSlots");
static_assert(EG13_MAX_MOD_OUTPUTS == kMaxModOutputs, "EG13_MAX_MOD_OUTPUTS must equal eg13::kMaxModOutputs");
static_assert(EG13_NUM_DESTINATIONS == kNumDestinations, "EG13_NUM_DESTINATIONS must equal eg13::kNumDestinations");
static_assert(EG13_NUM_SOURCES == kNumSources, "EG13_NUM_SOURCES must equal eg13::kNumSources");
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
  static_assert(sizeof(out->mod_value) == sizeof(snap.modValue), "mod_value layout");
  static_assert(sizeof(out->mod_out_min) == sizeof(snap.modOutMin), "mod_out_min layout");
  static_assert(sizeof(out->mod_out_max) == sizeof(snap.modOutMax), "mod_out_max layout");
  std::memcpy(out->mod_value, snap.modValue, sizeof out->mod_value);  // NaN and inf stay as they are
  std::memcpy(out->mod_out_min, snap.modOutMin, sizeof out->mod_out_min);
  std::memcpy(out->mod_out_max, snap.modOutMax, sizeof out->mod_out_max);
}

// ---- matrix names and routes ---------------------------------------------------------

bool validPart(uint32_t part) { return part < uint32_t(kNumParts); }

// The mod engine selected in a part's mod slot.
const ModEngineInfo& modEngineOf(const Synth& synth, int part, int slot) {
  return modEngineInfo(synth.engine(modSlotKind(slot), part));
}

// "Filter: Cutoff": the part's current engine names P p of the slot.
bool slotParamLabel(const Synth& synth, SlotKind kind, int part, int p, const char* prefix, char* buffer,
                    uint32_t size) {
  const int count = isModSlot(kind) ? modEngineOf(synth, part, modSlotIndex(kind)).engine.paramCount
                                    : slotEngineInfo(kind, synth.engine(kind, part)).paramCount;
  if (p < 0 || p >= count) return false;
  if (!buffer || size == 0) return false;
  std::snprintf(buffer, size, "%s: %s", prefix, synth.paramName(slotParamIndex(kind, part, p)));
  return true;
}

bool sourceName(void* context, uint32_t part, uint16_t code, char* buffer, uint32_t size) noexcept {
  if (!validPart(part) || code == kSrcNone || !buffer || size == 0) return false;
  if (code >= kSrcMod) {
    if (code >= kNumSources) return false;
    const int slot = (code - kSrcMod) / kMaxModOutputs;
    const int output = (code - kSrcMod) % kMaxModOutputs;
    const ModEngineInfo& info = modEngineOf(ctx(context).synth, int(part), slot);
    if (output >= info.outputCount) return false;
    if (info.outputCount == 1) {
      std::snprintf(buffer, size, "Mod %d %s", slot + 1, info.engine.displayName);
    } else {
      std::snprintf(buffer, size, "Mod %d %s %s", slot + 1, info.engine.displayName, info.outputs[output].name);
    }
    return true;
  }
  if (code > kSrcTransportPhase) return false;  // reserved
  eg13::sourceName(code, buffer, size);
  buffer[size - 1] = '\0';
  return true;
}

bool destinationName(void* context, uint32_t part, uint16_t code, char* buffer, uint32_t size) noexcept {
  if (!validPart(part) || code == kDestNone || !buffer || size == 0) return false;
  const Synth& synth = ctx(context).synth;
  const int ip = int(part);
  if (code == kDestPitch || code == kDestAmp || code == kDestPan || code == kDestPartLevel) {
    eg13::destinationName(code, buffer, size);
    buffer[size - 1] = '\0';
    return true;
  }
  if (code >= kDestSourceP && code < kDestFilterP) {
    return slotParamLabel(synth, SlotKind::Source, ip, code - kDestSourceP, "Source", buffer, size);
  }
  if (code >= kDestFilterP && code < kDestShaperP) {
    return slotParamLabel(synth, SlotKind::Filter, ip, code - kDestFilterP, "Filter", buffer, size);
  }
  if (code >= kDestShaperP && code < kDestPartFxP) {
    return slotParamLabel(synth, SlotKind::Shaper, ip, code - kDestShaperP, "Shaper", buffer, size);
  }
  if (code >= kDestPartFxP && code < kDestPartFxP + kPartFxParamCount) {
    return slotParamLabel(synth, SlotKind::PartFx, ip, code - kDestPartFxP, "Part FX", buffer, size);
  }
  if (code >= kDestModP && code < kNumDestinations) {
    const int slot = (code - kDestModP) / kModParamCount;
    char prefix[16];
    std::snprintf(prefix, sizeof prefix, "Mod %d", slot + 1);
    return slotParamLabel(synth, modSlotKind(slot), ip, (code - kDestModP) % kModParamCount, prefix, buffer, size);
  }
  return false;  // reserved
}

uint16_t destinationForParam(void*, uint32_t id) noexcept {
  // Part Level is the one Part-slot parameter a route can target.
  if (((id >> 8) & 0xFFu) == uint32_t(params::Slot::Part) && (id & 0xFFu) == uint32_t(PartParam::Level) &&
      (id >> 16) < uint32_t(kNumParts))
    return kDestPartLevel;
  const int32_t index = int32_t(paramIndex(id));
  if (index < 0) return kDestNone;
  if (size_t(index) >= size_t(kNumParts) * kParamsPerPart) return kDestNone;  // master
  const int part = int(size_t(index) / kParamsPerPart);
  struct Base {
    SlotKind kind;
    uint16_t dest;
  };
  static const Base bases[] = {{SlotKind::Source, kDestSourceP}, {SlotKind::Filter, kDestFilterP},
                               {SlotKind::Shaper, kDestShaperP}, {SlotKind::PartFx, kDestPartFxP},
                               {SlotKind::Mod1, modParamDestination(0, 0)}, {SlotKind::Mod2, modParamDestination(1, 0)},
                               {SlotKind::Mod3, modParamDestination(2, 0)}, {SlotKind::Mod4, modParamDestination(3, 0)}};
  for (const Base& b : bases) {
    const size_t first = slotParamIndex(b.kind, part, 0);
    const size_t count = slotParamCount(b.kind);
    if (size_t(index) >= first && size_t(index) < first + count) return uint16_t(b.dest + (size_t(index) - first));
  }
  return kDestNone;
}

bool routeGet(void* context, uint32_t part, uint32_t route, EG13Route* out) noexcept {
  if (!out || !validPart(part) || route >= uint32_t(kNumRoutes)) return false;
  const Synth& synth = ctx(context).synth;
  const Route r = synth.patch().routes[part][route];
  out->source = r.source;
  out->destination = r.destination;
  out->via = r.via;
  const size_t index = routeAmountParamIndex(int(part), int(route));
  out->amount = double(synth.param(paramSpec(index).id));
  return true;
}

bool routeSet(void* context, uint32_t part, uint32_t route, uint16_t source, uint16_t destination,
              uint16_t via) noexcept {
  if (!validPart(part) || route >= uint32_t(kNumRoutes)) return false;
  char name[EG13_TEXT_SIZE];
  if (source != kSrcNone && !sourceName(context, part, source, name, sizeof name)) return false;
  if (via != kSrcNone && !sourceName(context, part, via, name, sizeof name)) return false;
  if (destination != kDestNone && !destinationName(context, part, destination, name, sizeof name)) return false;
  CoreApiContext& c = ctx(context);
  Route r;
  r.source = source;
  r.destination = destination;
  r.via = via;
  c.synth.setRoute(int(part), int(route), r);  // main thread; publishPatch hands it to the audio thread
  if (c.markDirty) c.markDirty();
  return true;
}

bool version(void*, char* buffer, uint32_t size) noexcept {
  if (!buffer || size == 0) return false;
  std::snprintf(buffer, size, "%s (%s)", EG13_VERSION_STRING, EG13_GIT_HASH);
  return true;
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
  api.route_get = routeGet;
  api.route_set = routeSet;
  api.source_name = sourceName;
  api.destination_name = destinationName;
  api.destination_for_param = destinationForParam;
  api.version = version;
  api.request_resize = requestResize;
  return api;
}

}  // namespace eg13
