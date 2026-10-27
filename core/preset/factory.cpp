#include "preset/factory.h"

namespace eg13::preset {

// Defined by the generated factory_presets.cpp.
const FactoryPreset* generatedFactoryPresets(size_t& count);

namespace {
const FactoryPreset* gOverride = nullptr;
size_t gOverrideCount = 0;
bool gOverridden = false;
}  // namespace

const FactoryPreset* factoryPresets(size_t& count) {
  if (gOverridden) {
    count = gOverrideCount;
    return gOverride;
  }
  return generatedFactoryPresets(count);
}

void setFactoryPresetsForTesting(const FactoryPreset* presets, size_t count) {
  gOverridden = presets != nullptr;
  gOverride = presets;
  gOverrideCount = presets ? count : 0;
}

}  // namespace eg13::preset
