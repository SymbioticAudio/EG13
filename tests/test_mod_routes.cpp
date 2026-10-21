#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <string>

#include "mod/mod_routes.h"

using namespace eg13;

namespace {

std::string sourceText(uint16_t s) {
  char buf[32];
  sourceName(s, buf, sizeof(buf));
  return buf;
}

std::string destinationText(uint16_t d) {
  char buf[32];
  destinationName(d, buf, sizeof(buf));
  return buf;
}

}  // namespace

TEST_CASE("Matrix codes are fixed") {
  // Saved in projects: these values never change.
  REQUIRE(kSrcVelocity == 1);
  REQUIRE(kSrcReleaseVelocity == 2);
  REQUIRE(kSrcNote == 3);
  REQUIRE(kSrcGate == 4);
  REQUIRE(kSrcRandom == 5);
  REQUIRE(kSrcPressure == 6);
  REQUIRE(kSrcSlide == 7);
  REQUIRE(kSrcNoteBend == 8);
  REQUIRE(kSrcModWheel == 9);
  REQUIRE(kSrcPitchBend == 10);
  REQUIRE(kSrcChannelPressure == 11);
  REQUIRE(kSrcCcA == 12);
  REQUIRE(kSrcCcD == 15);
  REQUIRE(kSrcTransportPhase == 16);
  REQUIRE(modOutputSource(0, 0) == 32);
  REQUIRE(modOutputSource(3, 7) == 63);
  REQUIRE(kNumSources == 64);
  REQUIRE(kDestPitch == 1);
  REQUIRE(kDestAmp == 2);
  REQUIRE(kDestPan == 3);
  REQUIRE(kDestPartLevel == 4);
  REQUIRE(kDestSourceP == 16);
  REQUIRE(kDestFilterP == 32);
  REQUIRE(kDestShaperP == 48);
  REQUIRE(kDestPartFxP == 64);
  REQUIRE(modParamDestination(0, 0) == 80);
  REQUIRE(modParamDestination(3, 15) == 143);
  REQUIRE(kNumDestinations == 144);
}

TEST_CASE("Destinations sort into mod-slot, voice and part evaluations") {
  REQUIRE(destinationKind(kDestNone) == DestinationKind::None);
  REQUIRE(destinationKind(kDestPitch) == DestinationKind::Voice);
  REQUIRE(destinationKind(kDestAmp) == DestinationKind::Voice);
  REQUIRE(destinationKind(kDestPan) == DestinationKind::Voice);
  REQUIRE(destinationKind(kDestPartLevel) == DestinationKind::Part);
  REQUIRE(destinationKind(kDestSourceP + 15) == DestinationKind::Voice);
  REQUIRE(destinationKind(kDestFilterP) == DestinationKind::Voice);
  REQUIRE(destinationKind(kDestShaperP + 11) == DestinationKind::Voice);
  REQUIRE(destinationKind(kDestPartFxP) == DestinationKind::None);  // M5
  REQUIRE(destinationKind(modParamDestination(2, 5)) == DestinationKind::ModParam);
  REQUIRE(destinationKind(5) == DestinationKind::None);    // reserved
  REQUIRE(destinationKind(144) == DestinationKind::None);  // unknown, from a newer version
}

TEST_CASE("Routes multiply source, amount and via, and add per destination") {
  float source[kNumSources] = {};
  source[kSrcVelocity] = 0.8f;
  source[kSrcModWheel] = 0.5f;
  source[modOutputSource(1, 0)] = -0.25f;
  RouteSet routes;
  routes.route[0] = {kSrcVelocity, uint16_t(kDestFilterP + 0), kSrcNone};
  routes.amount[0] = 0.5f;
  routes.route[1] = {modOutputSource(1, 0), uint16_t(kDestFilterP + 0), kSrcModWheel};
  routes.amount[1] = 1.0f;
  routes.route[2] = {kSrcVelocity, kDestAmp, kSrcNone};
  routes.amount[2] = 0.0f;  // an Amp route that adds nothing still makes the Amp routed
  routes.route[3] = {kSrcVelocity, modParamDestination(0, 2), kSrcNone};
  routes.amount[3] = 1.0f;
  RouteSums sums;
  sumRoutes(routes, source, DestinationKind::Voice, &sums);
  REQUIRE(sums.value[kDestFilterP] == 0.8f * 0.5f + (-0.25f) * 1.0f * 0.5f);
  REQUIRE(sums.touched[kDestFilterP]);
  REQUIRE(sums.touched[kDestAmp]);
  REQUIRE(sums.value[kDestAmp] == 0.0f);
  REQUIRE_FALSE(sums.touched[modParamDestination(0, 2)]);  // not a voice destination
  REQUIRE_FALSE(sums.touched[kDestPitch]);
  sumRoutes(routes, source, DestinationKind::ModParam, &sums);
  REQUIRE(sums.touched[modParamDestination(0, 2)]);
  REQUIRE(sums.value[modParamDestination(0, 2)] == 0.8f);
  REQUIRE_FALSE(sums.touched[kDestFilterP]);
}

TEST_CASE("Empty, unknown and non-finite routes contribute nothing") {
  float source[kNumSources] = {};
  source[kSrcVelocity] = 1.0f;
  source[kSrcGate] = std::numeric_limits<float>::quiet_NaN();
  RouteSet routes;
  routes.route[0] = {kSrcNone, kDestAmp, kSrcNone};          // no source
  routes.route[1] = {kSrcVelocity, kDestNone, kSrcNone};     // no destination
  routes.route[2] = {200, kDestPitch, kSrcNone};             // unknown source
  routes.route[3] = {kSrcVelocity, kDestPan, 300};           // unknown via
  routes.route[4] = {kSrcGate, uint16_t(kDestSourceP + 1), kSrcNone};  // NaN source
  for (float& a : routes.amount) a = 1.0f;
  REQUIRE_FALSE(routeActive(routes.route[0]));
  REQUIRE_FALSE(routeActive(routes.route[1]));
  REQUIRE_FALSE(routeActive(routes.route[2]));
  REQUIRE_FALSE(routeActive(routes.route[3]));
  REQUIRE(routeActive(routes.route[4]));
  RouteSums sums;
  sumRoutes(routes, source, DestinationKind::Voice, &sums);
  REQUIRE_FALSE(sums.touched[kDestAmp]);
  REQUIRE_FALSE(sums.touched[kDestPitch]);
  REQUIRE_FALSE(sums.touched[kDestPan]);
  REQUIRE(sums.touched[kDestSourceP + 1]);
  REQUIRE(sums.value[kDestSourceP + 1] == 0.0f);
}

TEST_CASE("Modulated values add in normalized space and clamp") {
  RouteSums sums;
  std::fill(std::begin(sums.value), std::end(sums.value), 0.0f);
  std::fill(std::begin(sums.touched), std::end(sums.touched), false);
  REQUIRE(modulated(0.3f, sums, kDestFilterP) == 0.3f);
  sums.touched[kDestFilterP] = true;
  sums.value[kDestFilterP] = 0.9f;
  REQUIRE(modulated(0.3f, sums, kDestFilterP) == 1.0f);
  sums.value[kDestFilterP] = -0.9f;
  REQUIRE(modulated(0.3f, sums, kDestFilterP) == 0.0f);
  sums.value[kDestFilterP] = 0.25f;
  REQUIRE(modulated(0.5f, sums, kDestFilterP) == 0.75f);
}

TEST_CASE("Sources and destinations have names") {
  REQUIRE(sourceText(kSrcVelocity) == "Velocity");
  REQUIRE(sourceText(kSrcTransportPhase) == "Transport Phase");
  REQUIRE(sourceText(modOutputSource(1, 0)) == "Mod 2 Out 1");
  REQUIRE(sourceText(20) == "?");
  REQUIRE(destinationText(kDestPitch) == "Pitch");
  REQUIRE(destinationText(kDestFilterP) == "Filter P1");
  REQUIRE(destinationText(uint16_t(kDestShaperP + 11)) == "Shaper P12");
  REQUIRE(destinationText(modParamDestination(3, 15)) == "Mod 4 P16");
  REQUIRE(destinationText(kDestPartLevel) == "Part Level");
}

TEST_CASE("Reserved source codes are unknown and leave the Amp alone") {
  float source[kNumSources] = {};
  for (float& s : source) s = 1.0f;
  RouteSet routes;
  routes.route[0] = {17, kDestAmp, kSrcNone};
  routes.route[1] = {31, kDestAmp, kSrcNone};
  routes.route[2] = {kSrcVelocity, kDestAmp, 17};
  for (float& a : routes.amount) a = 1.0f;
  REQUIRE_FALSE(routeActive(routes.route[0]));
  REQUIRE_FALSE(routeActive(routes.route[1]));
  REQUIRE_FALSE(routeActive(routes.route[2]));
  REQUIRE(routeActive(Route{kSrcTransportPhase, kDestAmp, kSrcNone}));
  REQUIRE(routeActive(Route{modOutputSource(3, 7), kDestAmp, kSrcNone}));
  REQUIRE_FALSE(routeActive(Route{64, kDestAmp, kSrcNone}));
  RouteSums sums;
  sumRoutes(routes, source, DestinationKind::Voice, &sums);
  REQUIRE_FALSE(sums.touched[kDestAmp]);
  REQUIRE(sums.value[kDestAmp] == 0.0f);
}

TEST_CASE("Route arithmetic matches literal values") {
  float source[kNumSources] = {};
  source[kSrcVelocity] = 0.5f;
  source[kSrcModWheel] = 0.5f;
  source[modOutputSource(0, 0)] = -0.5f;
  RouteSet routes;
  routes.route[0] = {kSrcVelocity, kDestPan, kSrcNone};
  routes.amount[0] = 0.5f;  // +0.25
  routes.route[1] = {modOutputSource(0, 0), kDestPan, kSrcModWheel};
  routes.amount[1] = 0.5f;  // -0.125
  routes.route[2] = {kSrcVelocity, kDestPan, kSrcNone};
  routes.amount[2] = -1.0f;  // -0.5
  RouteSums sums;
  sumRoutes(routes, source, DestinationKind::Voice, &sums);
  REQUIRE(sums.value[kDestPan] == -0.375f);
  REQUIRE(modulated(0.5f, sums, kDestPan) == 0.125f);
  REQUIRE(modulated(0.25f, sums, kDestPan) == 0.0f);
}
