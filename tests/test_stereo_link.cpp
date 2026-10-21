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
