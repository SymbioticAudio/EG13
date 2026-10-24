#pragma once

#include "engine/param_format.h"
#include "engine/voice_engine.h"

namespace eg13 {

// Shared by every Part FX engine (Granular, Granular Delay, Resonator, Reverb; M5).

// A Part FX slot has 16 P's, like the Source slot.
constexpr int kPartFxParamCount = 16;
static_assert(kPartFxParamCount <= kMaxSlotParams, "Part FX P's must fit ParamValues");

// Stepped Part FX P's (Mode, Quality, Model, ...) are append-only option lists read in this many
// fixed steps, whatever the number of options, so a saved value keeps its meaning when options
// are appended. Never change it.
constexpr int kPartFxSelectorSteps = 16;

// The option a stepped P selects: step = normalized x 16 (0..15); steps past the last option read
// as the last option. NaN reads as the first option.
inline int partFxOption(float normalized, int options) {
  if (options <= 1) return 0;
  const int step = stepIndex(normalized, kPartFxSelectorSteps);
  return step < options ? step : options - 1;
}

// A switch P (Freeze) is on at 0.5 or more. NaN reads as off.
inline bool partFxSwitch(float normalized) { return clamp01(normalized) >= 0.5f; }

// The host default of each P position, whatever the Part FX type ("reset to default" sets these).
// Every Part FX engine's own defaults equal them: P1-P4 50 %, P5 75 % (Density), P6 50 %,
// P7-P12 0 %, P13-P16 50 %.
constexpr float kPartFxHostDefaults[kPartFxParamCount] = {0.5f, 0.5f, 0.5f, 0.5f, 0.75f, 0.5f, 0, 0,
                                                          0,    0,    0,    0,    0.5f,  0.5f, 0.5f, 0.5f};

}  // namespace eg13
