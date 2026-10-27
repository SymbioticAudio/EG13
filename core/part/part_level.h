#pragma once

#include <algorithm>

namespace eg13 {

// Part Level (task 8g, owner 2026-10-27): the knob runs from silence to +12 dB, so a quiet part
// gets its make-up gain here rather than in an EQ. The parameter (and everything that modulates
// it: a route adds in this 0..1 space and clamps) is the knob position v; the gain is
// (v / kPartLevelUnity)^3, a cubic fader law. 0 dB sits at kPartLevelUnity = 10^(-12/60), about
// 63 % of the knob, the default; the top is +12 dB; near 0 dB one percent of travel is 0.41 dB,
// -6 dB is at 50 %, -20 dB at 29 %. At the default the gain is exactly 1.
constexpr float kPartLevelUnity = 0.63095734f;
constexpr float kPartLevelMaxDb = 12.0f;

inline float partLevelGain(float position) {
  const float r = std::clamp(position, 0.0f, 1.0f) / kPartLevelUnity;
  return r * r * r;
}

}  // namespace eg13
