#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <set>

#include "engine/voice_engine.h"
#include "params/param_ids.h"

using namespace eg13;

TEST_CASE("Parameter IDs are stable") {
  STATIC_REQUIRE(params::id(0, params::Slot::Source, 1) == 0x00000101u);
  STATIC_REQUIRE(params::id(1, params::Slot::Mod4, 6) == 0x00010806u);
  STATIC_REQUIRE(params::kMasterLevel == 0x00FF0000u);
}

TEST_CASE("Parameter IDs are unique across parts, slots and indices") {
  std::set<uint32_t> ids;
  for (uint32_t part = 0; part < 2; ++part)
    for (uint32_t slot = 0; slot <= uint32_t(params::Slot::Matrix); ++slot)
      for (uint32_t index = 0; index < 16; ++index)
        ids.insert(params::id(part, params::Slot(slot), index));
  REQUIRE(ids.size() == 2u * 10u * 16u);
  REQUIRE(ids.count(params::kMasterLevel) == 0);
}

TEST_CASE("clamp01 clamps and maps NaN to 0") {
  REQUIRE(clamp01(-0.5f) == 0.0f);
  REQUIRE(clamp01(0.25f) == 0.25f);
  REQUIRE(clamp01(1.5f) == 1.0f);
  REQUIRE(clamp01(std::numeric_limits<float>::quiet_NaN()) == 0.0f);
}
