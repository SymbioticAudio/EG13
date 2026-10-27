#pragma once

#include <cstddef>

#include "preset/preset.h"

// The factory presets, compiled from presets/ at build time by eg13_preset into one table (the
// generated factory_presets.cpp under the build directory). Each json is a complete preset file.
namespace eg13::preset {

struct FactoryPreset {
  const char* bank;  // "Bank 1", "Bank 2", "FX"
  const char* name;
  const char* note;
  const char* credit;
  const char* licence;
  Kind kind;
  PluginKind plugin;
  bool firstLoad;  // a new instrument instance opens on this preset
  int order;       // position within the bank
  const char* json;
};

// The table, sorted by bank then order. count is 0 for an empty table (the pointer may be null).
const FactoryPreset* factoryPresets(size_t& count);

// Tests: use this table instead of the generated one; nullptr restores the generated table.
void setFactoryPresetsForTesting(const FactoryPreset* presets, size_t count);

}  // namespace eg13::preset
