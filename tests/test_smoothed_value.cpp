#include <catch2/catch_test_macros.hpp>

#include "util/smoothed_value.h"

using eg13::SmoothedValue;

TEST_CASE("SmoothedValue reset jumps immediately") {
  SmoothedValue s;
  s.prepare(48000.0, 0.01);
  s.reset(0.5f);
  REQUIRE(s.current() == 0.5f);
  REQUIRE(s.next() == 0.5f);
}

TEST_CASE("SmoothedValue moves monotonically and reaches the target exactly") {
  SmoothedValue s;
  s.prepare(48000.0, 0.01);
  s.reset(0.0f);
  s.setTarget(1.0f);
  float previous = 0.0f;
  for (int i = 0; i < 480; ++i) {  // one time constant
    const float v = s.next();
    REQUIRE(v >= previous);
    REQUIRE(v <= 1.0f);
    previous = v;
  }
  REQUIRE(previous > 0.6f);
  REQUIRE(previous < 0.7f);
  for (int i = 0; i < 48000; ++i) s.next();
  REQUIRE(s.current() == 1.0f);
}

TEST_CASE("SmoothedValue with zero time follows the target at once") {
  SmoothedValue s;
  s.prepare(48000.0, 0.0);
  s.reset(0.0f);
  s.setTarget(0.25f);
  REQUIRE(s.next() == 0.25f);
}
