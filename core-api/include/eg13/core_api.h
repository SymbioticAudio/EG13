/* The narrow C API between the EG13 plugin and its editors (spec §2.1 core-api, §8 Plumbing).
 * Pure C (it also compiles as C++), so SwiftUI today and other front-ends later can use it.
 * Every function is called on the main thread and returns without waiting on the audio thread.
 * Every function takes the table's `context` as its first argument. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* EG13_MAX_PARAMS is the parameter count (2 parts x 158 + 7 master); plugin/src/core_api.cpp
 * static_asserts it against eg13::kParamCount. */
enum { EG13_MAX_PARAMS = 323, EG13_NUM_PARTS = 2, EG13_TEXT_SIZE = 64 };

/* Matrix sizes; core_api.cpp static_asserts them against the core (kNumRoutes, kNumModSlots,
 * kMaxModOutputs, kNumDestinations, kNumSources). */
enum {
  EG13_NUM_ROUTES = 12,
  EG13_NUM_MOD_SLOTS = 4,
  EG13_MAX_MOD_OUTPUTS = 8,
  EG13_NUM_DESTINATIONS = 144,
  EG13_NUM_SOURCES = 64
};

/* Parameter ID: (part << 16) | (slot << 8) | index. Mirrors eg13::params::Slot, value for
 * value (core_api.cpp static_asserts each). Part 0xFF is the master. */
typedef enum EG13Slot {
  EG13_SLOT_PART = 0,
  EG13_SLOT_SOURCE = 1,
  EG13_SLOT_FILTER = 2,
  EG13_SLOT_SHAPER = 3,
  EG13_SLOT_PART_FX = 4,
  EG13_SLOT_MOD1 = 5,
  EG13_SLOT_MOD2 = 6,
  EG13_SLOT_MOD3 = 7,
  EG13_SLOT_MOD4 = 8,
  EG13_SLOT_MATRIX = 9
} EG13Slot;

#define EG13_MASTER_PART 0xFFu

static inline uint32_t eg13_param_id(uint32_t part, uint32_t slot, uint32_t index) {
  return (part << 16) | (slot << 8) | index;
}

/* The engine slots of a part. Mirrors eg13::SlotKind (table order, not parameter-ID slots). */
typedef enum EG13SlotKind {
  EG13_SLOT_KIND_SOURCE = 0,
  EG13_SLOT_KIND_FILTER = 1,
  EG13_SLOT_KIND_SHAPER = 2,
  EG13_SLOT_KIND_MOD1 = 3,
  EG13_SLOT_KIND_MOD2 = 4,
  EG13_SLOT_KIND_MOD3 = 5,
  EG13_SLOT_KIND_MOD4 = 6,
  EG13_SLOT_KIND_PART_FX = 7
} EG13SlotKind;

enum { EG13_PARAM_STEPPED = 1u << 0 };

typedef struct EG13ParamInfo {
  uint32_t id;
  uint32_t flags; /* EG13_PARAM_* */
  double min, max, def;
} EG13ParamInfo;

/* One matrix route. Source, destination and via are the core's codes (0 = none); the amount is
 * the route's host parameter value (-1..1), read from the snapshot or by param_index. */
typedef struct EG13Route {
  uint16_t source, destination, via;
  double amount;
} EG13Route;

typedef struct EG13Snapshot {
  uint32_t param_count;
  double values[EG13_MAX_PARAMS];     /* by parameter index */
  float part_peak[EG13_NUM_PARTS][2]; /* largest |sample| per part (L, R) since the last snapshot */
  int32_t active_voices[EG13_NUM_PARTS];
  uint32_t engine_generation; /* changes on every engine switch: UI, host or state load */
  /* Final normalized value (0..1) at each destination code: the last-started voice, or the part
   * for per-part destinations. Pitch is semitones / 48 + 0.5, Pan 0.5 at the centre. NaN: none. */
  float mod_value[EG13_NUM_PARTS][EG13_NUM_DESTINATIONS];
  /* Min and max of each mod output since the previous snapshot. +inf / -inf: no data. */
  float mod_out_min[EG13_NUM_PARTS][EG13_NUM_MOD_SLOTS][EG13_MAX_MOD_OUTPUTS];
  float mod_out_max[EG13_NUM_PARTS][EG13_NUM_MOD_SLOTS][EG13_MAX_MOD_OUTPUTS];
} EG13Snapshot;

/* A factory preset of this plugin's kind. The strings and json belong to the plugin and stay valid
 * for its lifetime. json is a complete preset file, ready for preset_apply. */
typedef struct EG13FactoryPreset {
  const char* bank; /* "Bank 1", "Bank 2", "FX" */
  const char* name;
  const char* note;
  const char* credit;
  const char* licence;
  uint32_t kind; /* 0 performance, 1 patch */
  const char* json;
} EG13FactoryPreset;

/* preset_apply results. */
enum { EG13_PRESET_OK = 0, EG13_PRESET_WARNINGS = 1, EG13_PRESET_INVALID = 2 };

/* Index arguments are parameter-table indices (0 .. param_count - 1), not parameter IDs.
 * Functions returning bool return false (and write nothing) for an argument out of range.
 * Text is written NUL-terminated, truncated to size. */
typedef struct EG13CoreAPI {
  void* context;
  uint32_t (*param_count)(void* context);
  bool (*param_info)(void* context, uint32_t index, EG13ParamInfo* info);
  /* Engine-aware: "Density" for Part FX P5 with Granular, "Spread" with Resonator. */
  bool (*param_name)(void* context, uint32_t index, char* buffer, uint32_t size);
  bool (*param_text)(void* context, uint32_t index, double value, char* buffer, uint32_t size);
  /* Table index of a parameter ID, or -1. */
  int32_t (*param_index)(void* context, uint32_t id);
  uint32_t (*engine_count)(void* context, uint32_t slot_kind);
  bool (*engine_name)(void* context, uint32_t slot_kind, uint32_t engine, char* buffer, uint32_t size);
  /* How many of the slot's P's the engine uses; the rest are unused. */
  uint32_t (*engine_param_count)(void* context, uint32_t slot_kind, uint32_t engine);
  /* The engine selected in the part's slot; 0 if out of range. */
  uint32_t (*current_engine)(void* context, uint32_t slot_kind, uint32_t part);
  /* Edits are host parameter changes: begin, any number of edits, end (spec §8). */
  void (*begin_edit)(void* context, uint32_t id);
  void (*edit)(void* context, uint32_t id, double value);
  void (*end_edit)(void* context, uint32_t id);
  /* Display rate. Also retries edits that did not fit the queue. */
  void (*snapshot)(void* context, EG13Snapshot* out);
  /* Routes: the patch's source, destination and via, plus the amount. False (nothing written)
   * for a part or route out of range. */
  bool (*route_get)(void* context, uint32_t part, uint32_t route, EG13Route* out);
  /* Sets a route's source, destination and via and marks the project dirty. Each code must be 0 or
   * one the part names (source_name / destination_name succeed). False and no change otherwise.
   * The amount is edited through begin_edit / edit / end_edit on the route's amount parameter. */
  bool (*route_set)(void* context, uint32_t part, uint32_t route, uint16_t source, uint16_t destination,
                    uint16_t via);
  /* Names for matrix pickers. False for 0, reserved codes, and codes the part's current engines
   * hide: "Velocity", "Mod 1 Envelope", "Mod 3 LFO Out 2"; "Pitch", "Filter: Cutoff", "Mod 2: Rate". */
  bool (*source_name)(void* context, uint32_t part, uint16_t code, char* buffer, uint32_t size);
  bool (*destination_name)(void* context, uint32_t part, uint16_t code, char* buffer, uint32_t size);
  /* The destination code of a P parameter ID (Source, Filter, Shaper, Part FX or mod P), else 0. */
  uint16_t (*destination_for_param)(void* context, uint32_t id);
  /* "0.6.0 (abc1234)": CMake project version and git short hash ("unknown" outside git). */
  bool (*version)(void* context, char* buffer, uint32_t size);
  /* Ask the host to resize the editor to width x height logical points. */
  void (*request_resize)(void* context, uint32_t width, uint32_t height);
  /* Presets (main thread). The factory list holds this plugin kind's presets only, in bank order
   * (Bank 1, Bank 2, FX) then the preset's order. */
  uint32_t (*factory_preset_count)(void* context);
  bool (*factory_preset)(void* context, uint32_t index, EG13FactoryPreset* out);
  /* Loads a preset file's sound: a Performance replaces both parts, a Patch goes into target_part.
   * The setup fields (MIDI channel, output, MPE, CCs, master level, voice gain, latency mode) are
   * kept. Marks the project dirty. Returns an EG13_PRESET_ result; for EG13_PRESET_INVALID nothing
   * changed and message (NUL-terminated, truncated to size; may be NULL) says why. */
  int32_t (*preset_apply)(void* context, const char* json, int32_t target_part /* -1 performance, 0 A, 1 B */,
                          char* message, uint32_t size);
  /* The current sound as a preset file: part -1 a Performance, 0 or 1 a Patch of that part, bank
   * "User". Returns the bytes needed including the NUL (0 for a bad part) and writes the file only
   * if it fits in size, so call once with size 0 to measure. */
  uint32_t (*preset_save)(void* context, int32_t part, const char* name, char* buffer, uint32_t size);
  /* The UI's browser state, an opaque string saved in the project ("" for none). False (and nothing
   * written) if it does not fit in size with its NUL. */
  bool (*browser_state_get)(void* context, char* buffer, uint32_t size);
  void (*browser_state_set)(void* context, const char* json);
} EG13CoreAPI;

#ifdef __cplusplus
}
#endif
