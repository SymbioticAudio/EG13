/* Compiled as C11 (tests/CMakeLists.txt) so the public headers cannot grow C++ constructs. */
#include "eg13/core_api.h"
#include "eg13/ui_entry.h"

_Static_assert(EG13_SLOT_MATRIX == 9, "EG13Slot");
_Static_assert(EG13_SLOT_KIND_PART_FX == 7, "EG13SlotKind");
_Static_assert(sizeof(EG13ParamInfo) == 32, "EG13ParamInfo layout");

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
  api->request_resize(api->context, 1200, 760);
  return sum;
}
