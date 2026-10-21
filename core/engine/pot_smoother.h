#pragma once

#include <cmath>

#include "engine/voice_engine.h"

namespace eg13 {

// Firmware pot smoothing as the modules run it: ONE_POLE(value, pot, c) once per control-rate
// update. Every engine that uses this updates once per native audio block, like its firmware.

// The coefficient for `count` samples of a one-pole that the firmware runs once per
// `nativeBlock` samples with coefficient `perBlock`. A partial block advances the filter by
// its share: (1 - c)^(count / nativeBlock) of the distance remains.
inline float blockCoefficient(float perBlock, int count, int nativeBlock) {
  if (count == nativeBlock) return perBlock;
  return 1.0f - std::pow(1.0f - perBlock, float(count) / float(nativeBlock));
}

// One pot one-pole per engine parameter, on the clamped 0..1 value. A coefficient of 1 marks
// a control the firmware does not smooth (stepped selectors, EG13-only controls): it passes
// straight through. The first update after reset() snaps every control to its target, so a
// render that holds its parameters is identical to an unsmoothed one.
class PotSmoother {
 public:
  // perBlock: one coefficient per parameter, `params` long, outliving the smoother.
  PotSmoother(const float* perBlock, int params, int nativeBlock)
      : perBlock_(perBlock), params_(params), nativeBlock_(nativeBlock) {}

  void reset() { snap_ = true; }

  // Advance by `count` native samples (1..nativeBlock) toward the targets in p.
  void update(const ParamValues& p, int count) {
    for (int i = 0; i < params_; ++i) {
      const float target = clamp01(p.p[i]);
      if (snap_ || perBlock_[i] >= 1.0f) {
        value_[i] = target;
      } else {
        value_[i] += blockCoefficient(perBlock_[i], count, nativeBlock_) * (target - value_[i]);
      }
    }
    snap_ = false;
  }

  float operator[](int i) const { return value_[i]; }

 private:
  const float* perBlock_;
  int params_;
  int nativeBlock_;
  float value_[kMaxSlotParams] = {};
  bool snap_ = true;
};

}  // namespace eg13
