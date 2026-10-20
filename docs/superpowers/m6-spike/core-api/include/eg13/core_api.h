/* The narrow C API between the EG13 plugin and its editors (spec §2.1 /core-api, §8 Plumbing).
 * Pure C, so SwiftUI today and other UI front-ends later can use it.
 * Every function is called on the main thread and returns without waiting on the audio thread. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { EG13_MAX_PARAMS = 128, EG13_NUM_PARTS = 2, EG13_TEXT_SIZE = 64 };

/* Parameter IDs: (part << 16) | (slot << 8) | index. Mirrors core/params/param_ids.h and
 * core/synth/param_table.h; plugin/src/ui_bridge.cpp static_asserts that they match. */
enum {
  EG13_SLOT_PART = 0,
  EG13_SLOT_SOURCE = 1,
  EG13_SLOT_FILTER = 2,
  EG13_SLOT_SHAPER = 3,
  EG13_SLOT_PART_FX = 4,
  EG13_MASTER_PART = 0xFF
};

enum {
  EG13_PART_LEVEL = 1,
  EG13_PART_PAN = 2,
  EG13_PART_GLIDE = 3,
  EG13_PART_TRANSPOSE = 4,
  EG13_PART_BASE_NOTE = 5,
  EG13_PART_MIDI_CHANNEL = 6,
  EG13_PART_KEY_LOW = 7,
  EG13_PART_KEY_HIGH = 8,
  EG13_PART_VOICE_MODE = 9,
  EG13_PART_VOICE_COUNT = 10,
  EG13_PART_TRIGGER = 11,
  EG13_PART_BEND_RANGE = 12,
  EG13_PART_MONO = 13,
  EG13_PART_OUTPUT = 14
};

enum { EG13_MASTER_LEVEL = 0, EG13_MASTER_DRY = 1, EG13_MASTER_MODE = 2, EG13_MASTER_SPLIT_NOTE = 3 };

static inline uint32_t eg13_param_id(uint32_t part, uint32_t slot, uint32_t index) {
  return (part << 16) | (slot << 8) | index;
}

enum {
  EG13_PARAM_STEPPED = 1u << 0,
  EG13_PARAM_HIDDEN = 1u << 1 /* not used by the selected engine */
};

typedef struct EG13ParamInfo {
  uint32_t id;
  uint32_t flags;
  double min_value;
  double max_value;
  double default_value;
} EG13ParamInfo;

typedef struct EG13Snapshot {
  uint32_t param_count;
  double values[EG13_MAX_PARAMS];          /* by parameter index */
  float part_peak[EG13_NUM_PARTS][2];      /* largest |sample| per part (L, R) since the last snapshot */
} EG13Snapshot;

typedef struct EG13CoreAPI {
  void* context;
  uint32_t (*param_count)(void* context);
  bool (*param_info)(void* context, uint32_t index, EG13ParamInfo* info);
  bool (*param_name)(void* context, uint32_t index, char* buffer, uint32_t size);
  bool (*param_text)(void* context, uint32_t index, double value, char* buffer, uint32_t size);
  uint32_t (*engine_count)(void* context, uint32_t slot);
  bool (*engine_name)(void* context, uint32_t slot, uint32_t engine, char* buffer, uint32_t size);
  /* Stable engine id (e.g. "plaits"); the editor keys per-engine UI metadata by it. */
  bool (*engine_id)(void* context, uint32_t slot, uint32_t engine, char* buffer, uint32_t size);
  /* Edits are CLAP parameter changes: begin, any number of edits, end (spec §8). */
  void (*begin_edit)(void* context, uint32_t id);
  void (*edit)(void* context, uint32_t id, double value);
  void (*end_edit)(void* context, uint32_t id);
  /* channel 0..15, key 0..127, velocity 0..1 (on-screen keyboard). */
  void (*note_on)(void* context, int32_t channel, int32_t key, double velocity);
  void (*note_off)(void* context, int32_t channel, int32_t key);
  /* Display rate. Also retries edits that did not fit the queue. */
  void (*snapshot)(void* context, EG13Snapshot* out);
  /* Ask the host to resize the editor to zoom (0.75..2.0) times its base size. */
  void (*request_zoom)(void* context, double zoom);
} EG13CoreAPI;

#ifdef __cplusplus
}
#endif
