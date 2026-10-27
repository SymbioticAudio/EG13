#pragma once

#include <array>
#include <cstdint>

#include "mod/mod_routes.h"
#include "synth/param_table.h"

namespace eg13 {

// What the editor reads on the main thread (Synth::uiSnapshot).
struct UiSnapshot {
  std::array<float, kParamCount> values{};
  float partPeak[kNumParts][2] = {};  // absolute peak since the last read, per part and channel
  int activeVoices[kNumParts] = {};
  // The final normalized value (0..1) at each destination code for the last-started voice, or the part
  // for the per-part destinations. Pitch is semitones / 48 + 0.5, Pan 0.5 at the centre. NaN: no value
  // (None, the reserved codes, a P of an Off mod slot).
  float modValue[kNumParts][kNumDestinations] = {};
  // Min and max of every mod output since the last uiSnapshot (read and reset, like partPeak). A
  // per-voice slot reads the last-started voice. +inf / -inf (min > max): nothing ran: no data.
  float modOutMin[kNumParts][kNumModSlots][kMaxModOutputs] = {};
  float modOutMax[kNumParts][kNumModSlots][kMaxModOutputs] = {};
  uint32_t engineGeneration = 0;      // changes on every engine switch: UI, host or state load
};

}  // namespace eg13
