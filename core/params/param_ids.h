#pragma once

#include <cstdint>

namespace eg13::params {

// Stable parameter IDs: (part << 16) | (slot << 8) | index.
// Index 0 is the slot's engine/type selector; 1..n are P1..Pn.
// IDs are saved in projects and presets and must never be renumbered.
enum class Slot : uint32_t {
  Part = 0,
  Source = 1,
  Filter = 2,
  Shaper = 3,
  PartFx = 4,
  Mod1 = 5,
  Mod2 = 6,
  Mod3 = 7,
  Mod4 = 8,
  Matrix = 9,
};

constexpr uint32_t kMasterPart = 0xFF;

constexpr uint32_t id(uint32_t part, Slot slot, uint32_t index) {
  return (part << 16) | (uint32_t(slot) << 8) | index;
}

constexpr uint32_t kMasterLevel = id(kMasterPart, Slot::Part, 0);

}  // namespace eg13::params
