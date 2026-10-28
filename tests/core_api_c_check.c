/* Compiled as C11 (tests/CMakeLists.txt) so the public headers cannot grow C++ constructs. */
#include "eg13/core_api.h"
#include "eg13/ui_entry.h"

_Static_assert(EG13_SLOT_MATRIX == 9, "EG13Slot");
_Static_assert(EG13_SLOT_KIND_PART_FX == 7, "EG13SlotKind");
_Static_assert(sizeof(EG13ParamInfo) == 32, "EG13ParamInfo layout");
_Static_assert(EG13_NUM_DESTINATIONS == 144, "EG13_NUM_DESTINATIONS");
_Static_assert(EG13_NUM_ROUTES == 12 && EG13_NUM_MOD_SLOTS == 4 && EG13_MAX_MOD_OUTPUTS == 8, "matrix sizes");
_Static_assert(sizeof(EG13Route) == 16, "EG13Route layout");

/* Exercises every table entry from C; the C++ test calls it with a real table. */
uint32_t eg13_test_c_walk(const EG13CoreAPI* api) {
  EG13ParamInfo info;
  EG13Snapshot snap;
  char text[EG13_TEXT_SIZE];
  uint32_t n = api->param_count(api->context);
  uint32_t sum = 0;
  uint32_t i;
  for (i = 0; i < n; ++i) {
    if (api->param_info(api->context, i, &info) && api->param_name(api->context, i, text, EG13_TEXT_SIZE) &&
        api->param_text(api->context, i, info.def, text, EG13_TEXT_SIZE) &&
        api->param_index(api->context, info.id) == (int32_t)i)
      ++sum;
  }
  api->begin_edit(api->context, eg13_param_id(0, EG13_SLOT_PART, 1));
  api->edit(api->context, eg13_param_id(0, EG13_SLOT_PART, 1), 0.5);
  api->end_edit(api->context, eg13_param_id(0, EG13_SLOT_PART, 1));
  api->snapshot(api->context, &snap);
  sum += api->engine_count(api->context, EG13_SLOT_KIND_PART_FX);
  sum += api->engine_param_count(api->context, EG13_SLOT_KIND_PART_FX, 1);
  sum += api->current_engine(api->context, EG13_SLOT_KIND_SOURCE, 0);
  {
    EG13Route route;
    uint16_t dest;
    if (api->route_get(api->context, 0, 0, &route) && api->route_set(api->context, 0, 0, route.source, route.destination, route.via))
      sum += (uint32_t)(route.amount > -2.0);
    if (api->source_name(api->context, 0, 1, text, EG13_TEXT_SIZE)) ++sum;
    if (api->destination_name(api->context, 0, 1, text, EG13_TEXT_SIZE)) ++sum;
    dest = api->destination_for_param(api->context, eg13_param_id(0, EG13_SLOT_FILTER, 1));
    sum += (uint32_t)(dest != 0);
    if (api->version(api->context, text, EG13_TEXT_SIZE)) ++sum;
    snap.mod_value[1][EG13_NUM_DESTINATIONS - 1] = 0.0f;
    snap.mod_out_min[1][EG13_NUM_MOD_SLOTS - 1][EG13_MAX_MOD_OUTPUTS - 1] = 0.0f;
    snap.mod_out_max[1][EG13_NUM_MOD_SLOTS - 1][EG13_MAX_MOD_OUTPUTS - 1] = 0.0f;
  }
  {
    EG13FactoryPreset preset;
    char message[EG13_TEXT_SIZE];
    char file[16];
    if (api->factory_preset_count(api->context) > 0 && api->factory_preset(api->context, 0, &preset))
      sum += (uint32_t)(preset.kind + (preset.json != 0));
    sum += (uint32_t)(api->preset_apply(api->context, "{}", -1, message, EG13_TEXT_SIZE) == EG13_PRESET_INVALID);
    sum += api->preset_save(api->context, -1, "x", file, 0) > 0;
    api->browser_state_set(api->context, "{}");
    sum += api->preset_init != 0;
    sum += api->param_is_setup != 0;
    if (api->browser_state_get(api->context, message, EG13_TEXT_SIZE)) ++sum;
    sum += (uint32_t)(EG13_PRESET_OK + EG13_PRESET_WARNINGS);
  }
  api->request_resize(api->context, 1200, 760);
  return sum;
}
