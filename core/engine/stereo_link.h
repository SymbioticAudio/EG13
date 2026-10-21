#pragma once

#include <algorithm>

namespace eg13 {

// Spec §5.1: a Filter or Shaper runs a second (right-channel) instance only while its input is
// stereo.
constexpr double kStereoFadeSeconds = 0.01;  // right output fades between instances
constexpr double kStereoHoldSeconds = 0.05;  // L == R this long before mono again

// Decides, per engine, when its right instance runs and what the right output is.
// Starting: a copyable engine gives the right instance the left one's state (CopyLeft) and the
// right output switches to it at once, seamlessly; any other engine resets its right instance
// (ResetRight) and the right output fades from the left output to it. Stopping: the right
// output fades back to the left output, then the right instance stops. A restart during a fade
// continues from the current weight with the instance still running.
class StereoLink {
 public:
  enum class Start { None, CopyLeft, ResetRight };

  void prepare(double rate) { step_ = float(1.0 / std::max(1.0, rate * kStereoFadeSeconds)); }

  void reset() {
    running_ = false;
    weight_ = 0.0f;
    target_ = 0.0f;
  }

  // Once per block, before processing. The caller copies or resets its right instance as told.
  Start request(bool stereo, bool copyable) {
    target_ = stereo ? 1.0f : 0.0f;
    if (!stereo || running_) return Start::None;
    running_ = true;
    if (copyable) {
      weight_ = 1.0f;
      return Start::CopyLeft;
    }
    weight_ = 0.0f;
    return Start::ResetRight;
  }

  bool rightRuns() const { return running_; }
  float weight() const { return weight_; }

  // out = left + weight * (right - left), per sample; exactly right at full weight (the sum
  // can round past right, and past the engine's range). right is not read while not running.
  // out may alias right.
  void mix(const float* left, const float* right, float* out, int n) {
    if (!running_) {
      std::copy(left, left + n, out);
      return;
    }
    for (int i = 0; i < n; ++i) {
      weight_ += std::clamp(target_ - weight_, -step_, step_);
      out[i] = weight_ >= 1.0f ? right[i] : left[i] + weight_ * (right[i] - left[i]);
    }
    if (target_ == 0.0f && weight_ <= 0.0f) {
      weight_ = 0.0f;
      running_ = false;
    }
  }

 private:
  bool running_ = false;
  float weight_ = 0.0f;
  float target_ = 0.0f;
  float step_ = 1.0f;
};

// Whether a slot's input is stereo: from the first block with any L != R sample; mono again
// once L == R exactly (silence included) has held for kStereoHoldSeconds.
class StereoDetector {
 public:
  void prepare(double rate) { hold_ = int(rate * kStereoHoldSeconds); }

  void reset() {
    stereo_ = false;
    same_ = 0;
  }

  bool update(const float* l, const float* r, int n) {
    bool differs = false;
    for (int i = 0; i < n; ++i) differs = differs || l[i] != r[i];
    if (differs) {
      stereo_ = true;
      same_ = 0;
    } else if (stereo_) {
      same_ += n;
      if (same_ >= hold_) stereo_ = false;
    }
    return stereo_;
  }

  bool stereo() const { return stereo_; }

 private:
  bool stereo_ = false;
  int same_ = 0;
  int hold_ = 2400;
};

}  // namespace eg13
