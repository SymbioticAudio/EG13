#pragma once

#include <array>
#include <cstdint>

#include "synth/param_table.h"

namespace eg13 {

// What the editor reads on the main thread (Synth::uiSnapshot).
struct UiSnapshot {
  std::array<float, kParamCount> values{};
  float partPeak[kNumParts][2] = {};  // absolute peak since the last read, per part and channel
  int activeVoices[kNumParts] = {};
  uint32_t engineGeneration = 0;      // changes on every engine switch: UI, host or state load
};

}  // namespace eg13
