#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

#include "util/soft_clip.h"

using namespace eg13;

TEST_CASE("The soft clipper leaves everything below -3 dBFS untouched") {
  for (float x = -0.708f; x <= 0.708f; x += 0.001f) {
    if (std::fabs(x) <= kSoftClipThreshold) REQUIRE(softClip(x) == x);
  }
  REQUIRE(softClip(0.0f) == 0.0f);
  REQUIRE(std::fabs(20.0f * std::log10(kSoftClipThreshold) + 3.0f) < 1e-4f);
}

TEST_CASE("The soft clipper is smooth, odd, rising and never exceeds full scale") {
  float previous = softClip(kSoftClipThreshold);
  for (float x = kSoftClipThreshold + 1e-4f; x < 2.9f; x += 1e-4f) {
    const float y = softClip(x);
    REQUIRE(y >= previous);  // never falls
    REQUIRE(y < 1.0f);       // below full scale (from about 3x full scale float rounds it to 1)
    REQUIRE(softClip(-x) == -y);
    previous = y;
  }
  // Slope 1 at the knee: no corner.
  const float h = 1e-3f;
  const float slope = (softClip(kSoftClipThreshold + h) - softClip(kSoftClipThreshold)) / h;
  REQUIRE(std::fabs(slope - 1.0f) < 0.01f);
  REQUIRE(softClip(1.0f) < 0.96f);  // a full-scale peak is gently reduced
  REQUIRE(softClip(1e6f) <= 1.0f);
  REQUIRE(softClip(-1e6f) >= -1.0f);
  REQUIRE(softClip(INFINITY) == 1.0f);
}

TEST_CASE("The soft clipper is continuous at the knee in value and slope") {
  const float k = kSoftClipThreshold;
  const float e = 1e-4f;
  for (float sign : {1.0f, -1.0f}) {
    const float below = softClip(sign * (k - e));
    const float at = softClip(sign * k);
    const float above = softClip(sign * (k + e));
    REQUIRE(std::fabs(at - sign * k) < 1e-6f);
    REQUIRE(std::fabs(above - at) < 2.0f * e);  // no jump
    REQUIRE(std::fabs(at - below) < 2.0f * e);
    // Slope on each side of the knee is 1 (the second difference is the only change).
    const float slopeBelow = sign * (at - below) / e;
    const float slopeAbove = sign * (above - at) / e;
    REQUIRE(std::fabs(slopeBelow - 1.0f) < 0.05f);
    REQUIRE(std::fabs(slopeAbove - 1.0f) < 0.05f);
  }
}

TEST_CASE("The soft clipper stays inside full scale for any input and survives NaN") {
  for (float x : {1.0f, 2.0f, 10.0f, 1e3f, 1e6f, 1e30f, std::numeric_limits<float>::max(),
                  std::numeric_limits<float>::infinity()}) {
    REQUIRE(std::fabs(softClip(x)) <= 1.0f);
    REQUIRE(std::fabs(softClip(-x)) <= 1.0f);
  }
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float y = softClip(nan);
  REQUIRE(std::isfinite(y));
  REQUIRE(std::fabs(y) <= 1.0f);
  REQUIRE(std::isfinite(softClip(-nan)));
}
