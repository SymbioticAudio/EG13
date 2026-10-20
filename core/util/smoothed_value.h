#pragma once

#include <cmath>

namespace eg13 {

// One-pole parameter smoother. Snaps to the target once within 1e-6 so a
// level of zero becomes exactly zero.
class SmoothedValue {
 public:
  void prepare(double sampleRate, double timeSeconds) {
    coeff_ = timeSeconds > 0.0 ? float(std::exp(-1.0 / (timeSeconds * sampleRate))) : 0.0f;
  }

  void reset(float value) {
    current_ = value;
    target_ = value;
  }

  void setTarget(float value) { target_ = value; }

  float next() {
    const float value = target_ + (current_ - target_) * coeff_;
    current_ = (value == current_ || std::fabs(value - target_) < 1e-6f) ? target_ : value;
    return current_;
  }

  float current() const { return current_; }
  float target() const { return target_; }

 private:
  float coeff_ = 0.0f;
  float current_ = 0.0f;
  float target_ = 0.0f;
};

}  // namespace eg13
