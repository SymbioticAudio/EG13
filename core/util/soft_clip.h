#pragma once

#include <cmath>

namespace eg13 {

// The master output's soft clipper (spec §6, owner 2026-10-20; provisional ruling 23): unchanged
// up to -3 dBFS, then a tanh knee that leaves the straight line with the same slope and
// approaches full scale, never exceeding it (in float it reaches exactly 1.0 from about 3x full
// scale, +9.5 dBFS, on). Applied per channel. NaN gives 0.
constexpr float kSoftClipThreshold = 0.70794578f;  // 10^(-3/20)

inline float softClip(float x) {
  if (std::isnan(x)) return 0.0f;
  const float a = std::fabs(x);
  if (!(a > kSoftClipThreshold)) return x;
  constexpr float kHeadroom = 1.0f - kSoftClipThreshold;
  return std::copysign(kSoftClipThreshold + kHeadroom * std::tanh((a - kSoftClipThreshold) / kHeadroom), x);
}

}  // namespace eg13
