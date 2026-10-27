#pragma once

#include <cmath>

namespace eg13 {

// The Shaper's DC filter (task 8g): a second-order Butterworth high-pass at kCornerHz, in double
// (the poles sit close to z = 1 at high host rates), transposed direct form II. Two zeros at DC,
// so a constant offset goes completely; -0.007 dB at 40 Hz. A one-pole at ~10 Hz would cost
// 0.26 dB at 40 Hz (the EQ must stay within 0.01 dB there), and one low enough for that (2 Hz)
// takes ~80 ms to settle. No allocation; a state that decays below 1e-30 is flushed to zero.
class DcBlocker {
 public:
  static constexpr double kCornerHz = 8.0;

  void prepare(double rate) {
    const double w = 2.0 * 3.14159265358979323846 * kCornerHz / rate;
    const double c = std::cos(w);
    const double alpha = std::sin(w) / (2.0 * 0.70710678118654752);  // Q = 1/sqrt(2)
    const double a0 = 1.0 + alpha;
    b0_ = 0.5 * (1.0 + c) / a0;
    b1_ = -(1.0 + c) / a0;
    a1_ = -2.0 * c / a0;
    a2_ = (1.0 - alpha) / a0;
    reset();
  }
  void reset() { s1_ = s2_ = 0.0; }

  void process(float* x, int n) {
    for (int i = 0; i < n; ++i) {
      const double in = double(x[i]);
      const double y = b0_ * in + s1_;
      s1_ = b1_ * in - a1_ * y + s2_;
      s2_ = b0_ * in - a2_ * y;  // b2 == b0
      x[i] = float(y);
    }
    if (std::fabs(s1_) < 1e-30) s1_ = 0.0;
    if (std::fabs(s2_) < 1e-30) s2_ = 0.0;
  }

 private:
  double b0_ = 1.0, b1_ = 0.0, a1_ = 0.0, a2_ = 0.0;
  double s1_ = 0.0, s2_ = 0.0;
};

}  // namespace eg13
