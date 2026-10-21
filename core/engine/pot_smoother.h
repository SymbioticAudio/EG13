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

// De-click time for the EG13-only gain/mix controls (Aux Mix, Spread).
constexpr double kMixDeclickSeconds = 0.005;

// Per-sample de-click one-pole for EG13-only gain and mix controls, which have no firmware
// filter to reproduce. The first value after reset() snaps to its target; afterwards it snaps
// once within 1e-6, or once a step no longer moves the float (it would stall short).
class Declicker {
 public:
  explicit Declicker(double seconds, double rate = 48000.0) : seconds_(seconds) {
    prepare(rate);
  }

  void prepare(double rate) {
    coeff_ = seconds_ > 0.0 ? float(1.0 - std::exp(-1.0 / (seconds_ * rate))) : 1.0f;
  }

  void reset() { snap_ = true; }

  float next(float target) {
    if (snap_) {
      value_ = target;
      snap_ = false;
      return value_;
    }
    const float smoothed = value_ + coeff_ * (target - value_);
    value_ = smoothed == value_ || std::fabs(target - smoothed) < 1e-6f ? target : smoothed;
    return value_;
  }

  float value() const { return value_; }

 private:
  double seconds_;
  float coeff_ = 1.0f;
  float value_ = 0.0f;
  bool snap_ = true;
};

}  // namespace eg13
