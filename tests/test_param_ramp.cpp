#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>

#include "engine/param_format.h"
#include "engine/pot_smoother.h"

using namespace eg13;

TEST_CASE("ParamRamp holds its first target after reset") {
  ParamRamp ramp;
  ramp.reset();
  ramp.begin(0.8f, 16);
  for (int i = 0; i < 16; ++i) REQUIRE(ramp.at(i) == 0.8f);
  REQUIRE(ramp.value() == 0.8f);
}

TEST_CASE("ParamRamp steps gradually and ends each call exactly on its target") {
  ParamRamp ramp;
  ramp.reset();
  ramp.begin(0.2f, 16);
  ramp.begin(0.8f, 16);
  const float step = (0.8f - 0.2f) / 16.0f;
  float previous = 0.2f;
  for (int i = 0; i < 15; ++i) {
    REQUIRE(ramp.at(i) == 0.2f + step * float(i + 1));
    REQUIRE(ramp.at(i) > previous);
    previous = ramp.at(i);
  }
  REQUIRE(ramp.at(15) == 0.8f);
  ramp.begin(0.8f, 7);  // converged: holds
  for (int i = 0; i < 7; ++i) REQUIRE(ramp.at(i) == 0.8f);
}

TEST_CASE("ParamRamp snaps again after reset") {
  ParamRamp ramp;
  ramp.reset();
  ramp.begin(0.1f, 16);
  ramp.reset();
  ramp.begin(0.9f, 16);
  REQUIRE(ramp.at(0) == 0.9f);
}

TEST_CASE("Shared formatters and knob curve") {
  REQUIRE(knobHz(0.0f) == 20.0f);
  REQUIRE(std::fabs(knobHz(1.0f) - 20000.0f) < 0.5f);
  REQUIRE(std::fabs(knobHz(0.5f) - 632.456f) < 0.01f);
  REQUIRE(knobHz(std::nanf("")) == 20.0f);
  char buf[32];
  formatHz(440.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "440 Hz");
  formatHz(1500.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "1.50 kHz");
  formatDb(-6.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "-6.0 dB");
  formatSemitones(7.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "+7.0 st");
  formatPercent01(0.25f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "25%");
  REQUIRE(stepIndex(0.0f, 3) == 0);
  REQUIRE(stepIndex(0.34f, 3) == 1);
  REQUIRE(stepIndex(1.0f, 3) == 2);
  REQUIRE(stepIndex(std::nanf(""), 3) == 0);
}

TEST_CASE("Shared Q curve and Hz knob text") {
  REQUIRE(knobQ(0.0f) == 0.5f);
  REQUIRE(std::fabs(knobQ(1.0f) - 40.0f) < 1e-4f);
  REQUIRE(knobQ(std::nanf("")) == 0.5f);
  char buf[32];
  formatKnobHz(0.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "20 Hz");
  formatKnobHz(1.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "20.00 kHz");
}
