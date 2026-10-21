#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include "engine/stereo_link.h"

using namespace eg13;

namespace {
constexpr double kRate = 48000.0;
constexpr int kFade = 480;  // 10 ms at 48 kHz
}  // namespace

TEST_CASE("StereoDetector turns stereo at the first differing sample and mono after the hold") {
  StereoDetector d;
  d.prepare(kRate);
  d.reset();
  std::vector<float> l(16, 0.5f), r(16, 0.5f);
  REQUIRE_FALSE(d.update(l.data(), r.data(), 16));
  r[9] = 0.4f;
  REQUIRE(d.update(l.data(), r.data(), 16));
  r[9] = 0.5f;
  int samples = 0;
  while (d.update(l.data(), r.data(), 16)) samples += 16;
  REQUIRE(samples + 16 >= int(kRate * kStereoHoldSeconds));
  REQUIRE(samples <= int(kRate * kStereoHoldSeconds));
  d.reset();
  REQUIRE_FALSE(d.stereo());
}

TEST_CASE("StereoLink while mono: the right output is the left output") {
  StereoLink link;
  link.prepare(kRate);
  link.reset();
  REQUIRE(link.request(false, true) == StereoLink::Start::None);
  REQUIRE_FALSE(link.rightRuns());
  std::vector<float> left = {0.1f, 0.2f, 0.3f}, right = {9.f, 9.f, 9.f}, out(3);
  link.mix(left.data(), right.data(), out.data(), 3);
  REQUIRE(out == left);
}

TEST_CASE("StereoLink start with a copyable engine is immediate") {
  StereoLink link;
  link.prepare(kRate);
  link.reset();
  REQUIRE(link.request(true, true) == StereoLink::Start::CopyLeft);
  REQUIRE(link.rightRuns());
  std::vector<float> left(16, 0.1f), right(16, 0.7f), out(16);
  link.mix(left.data(), right.data(), out.data(), 16);
  for (float s : out) REQUIRE(s == 0.7f);
  REQUIRE(link.request(true, true) == StereoLink::Start::None);
}

TEST_CASE("StereoLink start and stop are continuous") {
  StereoLink link;
  link.prepare(kRate);
  link.reset();
  REQUIRE(link.request(true, false) == StereoLink::Start::ResetRight);
  std::vector<float> left(16, 0.0f), right(16, 1.0f), out(16);
  float previous = 0.0f;
  int samples = 0;
  for (int block = 0; block < 40; ++block) {  // fade in
    link.request(true, false);
    link.mix(left.data(), right.data(), out.data(), 16);
    for (float s : out) {
      REQUIRE(s >= previous);
      REQUIRE(s - previous <= 1.0f / float(kFade) + 1e-6f);
      previous = s;
      ++samples;
    }
  }
  REQUIRE(previous == 1.0f);
  for (int block = 0; block < 40; ++block) {  // fade out
    link.request(false, false);
    link.mix(left.data(), right.data(), out.data(), 16);
    for (float s : out) {
      REQUIRE(s <= previous);
      REQUIRE(previous - s <= 1.0f / float(kFade) + 1e-6f);
      previous = s;
    }
  }
  REQUIRE(previous == 0.0f);
  REQUIRE_FALSE(link.rightRuns());
}

TEST_CASE("StereoLink restarting during a fade-out continues from the current weight") {
  StereoLink link;
  link.prepare(kRate);
  link.reset();
  link.request(true, true);
  std::vector<float> left(16, 0.0f), right(16, 1.0f), out(16);
  link.mix(left.data(), right.data(), out.data(), 16);
  link.request(false, true);
  link.mix(left.data(), right.data(), out.data(), 16);
  const float w = link.weight();
  REQUIRE(w < 1.0f);
  REQUIRE(link.request(true, true) == StereoLink::Start::None);  // still running: no copy
  link.mix(left.data(), right.data(), out.data(), 16);
  REQUIRE(std::fabs(out[0] - (w + 1.0f / float(kFade))) < 1e-5f);
}

TEST_CASE("StereoLink can mix in place over the right buffer") {
  StereoLink link;
  link.prepare(kRate);
  link.reset();
  link.request(true, true);
  std::vector<float> left(8, 0.25f), right(8, 0.5f);
  link.mix(left.data(), right.data(), right.data(), 8);
  for (float s : right) REQUIRE(s == 0.5f);
}

// Found by the Ring Mod / Vocoder range test: left + 1 * (right - left) can round past right
// (here 28659 / 10240 and -3.2 give -3.2000003, outside Warps' 16-bit range).
TEST_CASE("StereoLink at full weight outputs the right instance exactly") {
  StereoLink link;
  link.prepare(kRate);
  link.reset();
  link.request(true, false);
  std::vector<float> left(size_t(kFade) * 2, 28659.0f / 10240.0f), right(size_t(kFade) * 2, -3.2f), out(size_t(kFade) * 2);
  link.mix(left.data(), right.data(), out.data(), kFade * 2);
  REQUIRE(link.weight() == 1.0f);
  for (size_t i = size_t(kFade); i < out.size(); ++i) REQUIRE(out[i] == -3.2f);
}

TEST_CASE("StereoLink stop after a CopyLeft start fades back to the left output, then stops") {
  StereoLink link;
  link.prepare(kRate);
  link.reset();
  REQUIRE(link.request(true, true) == StereoLink::Start::CopyLeft);
  std::vector<float> left(16, 0.0f), right(16, 1.0f), out(16);
  link.mix(left.data(), right.data(), out.data(), 16);
  REQUIRE(out.back() == 1.0f);
  float previous = 1.0f;
  int samples = 0;
  while (link.rightRuns()) {
    REQUIRE(link.request(false, true) == StereoLink::Start::None);
    link.mix(left.data(), right.data(), out.data(), 16);
    for (float s : out) {
      REQUIRE(s <= previous);
      REQUIRE(previous - s <= 1.0f / float(kFade) + 1e-6f);
      previous = s;
    }
    samples += 16;
    REQUIRE(samples <= kFade + 16);
  }
  REQUIRE(samples >= kFade);
  REQUIRE(previous == 0.0f);
  link.mix(left.data(), right.data(), out.data(), 16);  // stopped: the left output
  REQUIRE(out == left);
}

TEST_CASE("StereoDetector and StereoLink together: stereo, mono, then stereo again") {
  constexpr double kTwoPi = 6.283185307179586;
  // Mono, stereo from 1600, mono from 3200 (past the 2400-sample hold and the 480-sample fade),
  // stereo again from 6400. R drifts smoothly away from L, so the input itself never jumps.
  const auto right = [kTwoPi](int t, float l) {
    const int start = t >= 6400 ? 6400 : 1600;
    const bool stereo = (t >= 1600 && t < 3200) || t >= 6400;
    return stereo ? l * float(1.0 + 0.5 * std::sin(0.5 * kTwoPi * double(t - start) / 1600.0)) : l;
  };
  for (bool copyable : {true, false}) {
    INFO((copyable ? "copyable" : "not copyable"));
    StereoDetector detector;
    detector.prepare(kRate);
    detector.reset();
    StereoLink link;
    link.prepare(kRate);
    link.reset();
    int starts = 0;
    float previous = 0.0f;
    float largest = 0.0f;
    for (int block = 0; block < 600; ++block) {
      // The instances: the left one outputs L. The right one outputs R, exactly as a copied
      // state continues; a reset instance differs (here by 0.25), which only its fades hide.
      float l[16], r[16], rightOut[16], out[16];
      for (int i = 0; i < 16; ++i) {
        const int t = block * 16 + i;
        l[i] = 0.5f * float(std::sin(kTwoPi * 220.0 * double(t) / kRate));
        r[i] = right(t, l[i]);
        rightOut[i] = copyable ? r[i] : r[i] + 0.25f;
      }
      const StereoLink::Start start = link.request(detector.update(l, r, 16), copyable);
      if (start != StereoLink::Start::None) {
        ++starts;
        REQUIRE(start == (copyable ? StereoLink::Start::CopyLeft : StereoLink::Start::ResetRight));
      }
      link.mix(l, rightOut, out, 16);
      for (float s : out) {
        largest = std::max(largest, std::fabs(s - previous));
        previous = s;
      }
      const int t = block * 16;
      if (t == 1600 + 160) REQUIRE(link.rightRuns());      // stereo
      if (t == 3200 + 2000) REQUIRE(link.rightRuns());     // mono, but within the hold
      if (t == 6400 - 160) REQUIRE_FALSE(link.rightRuns());  // mono: the right instance stopped
      if (t == 6400 + 160) REQUIRE(link.rightRuns());      // stereo again
    }
    REQUIRE(starts == 2);
    // R steps by up to 0.022 per sample (220 Hz at 0.75); a cut would add up to 0.25.
    REQUIRE(largest < 0.04f);
  }
}
