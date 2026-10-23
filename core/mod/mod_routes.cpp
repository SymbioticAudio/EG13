#include "mod/mod_routes.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>

namespace eg13 {

namespace {

const char* const kFixedSources[] = {"None",           "Velocity",   "Release Velocity", "Note",
                                     "Gate",           "Random",     "Pressure",         "Slide",
                                     "Note Bend",      "Mod Wheel",  "Pitch Bend",       "Channel Pressure",
                                     "CC A",           "CC B",       "CC C",             "CC D",
                                     "Transport Phase"};
constexpr int kNumFixedSourceNames = int(sizeof(kFixedSources) / sizeof(kFixedSources[0]));

// 17..31 are reserved gaps: unknown, so inactive (kept on resave, ignored).
bool knownSource(uint16_t s) {
  return (s >= kSrcVelocity && s <= kSrcTransportPhase) || (s >= kSrcMod && s < kNumSources);
}

}  // namespace

bool operator==(const Route& a, const Route& b) {
  return a.source == b.source && a.destination == b.destination && a.via == b.via;
}

DestinationKind destinationKind(uint16_t d) {
  if (d == kDestPartLevel) return DestinationKind::Part;
  if (d == kDestPitch || d == kDestAmp || d == kDestPan) return DestinationKind::Voice;
  if (d >= kDestSourceP && d < kDestPartFxP) return DestinationKind::Voice;  // Source, Filter, Shaper P's
  if (d >= kDestPartFxP && d < kDestPartFxP + kPartFxParamCount) return DestinationKind::Part;
  if (d >= kDestModP && d < kNumDestinations) return DestinationKind::ModParam;
  return DestinationKind::None;
}

bool routeActive(const Route& r) {
  return knownSource(r.source) && destinationKind(r.destination) != DestinationKind::None &&
         (r.via == kSrcNone || knownSource(r.via));
}

void sumRoutes(const RouteSet& routes, const float* source, DestinationKind kind, RouteSums* out) {
  std::fill(std::begin(out->value), std::end(out->value), 0.0f);
  std::fill(std::begin(out->touched), std::end(out->touched), false);
  for (int r = 0; r < kNumRoutes; ++r) {
    const Route& route = routes.route[r];
    if (!routeActive(route) || destinationKind(route.destination) != kind) continue;
    float v = source[route.source] * routes.amount[r];
    if (route.via != kSrcNone) v *= source[route.via];
    out->value[route.destination] += std::isfinite(v) ? v : 0.0f;
    out->touched[route.destination] = true;
  }
}

void sourceName(uint16_t s, char* buf, size_t size) {
  if (s < kNumFixedSourceNames) {
    std::snprintf(buf, size, "%s", kFixedSources[s]);
  } else if (s >= kSrcMod && s < kNumSources) {
    const int slot = (s - kSrcMod) / kMaxModOutputs;
    std::snprintf(buf, size, "Mod %d Out %d", slot + 1, (s - kSrcMod) % kMaxModOutputs + 1);
  } else {
    std::snprintf(buf, size, "?");
  }
}

void destinationName(uint16_t d, char* buf, size_t size) {
  if (d == kDestNone) {
    std::snprintf(buf, size, "None");
  } else if (d == kDestPitch) {
    std::snprintf(buf, size, "Pitch");
  } else if (d == kDestAmp) {
    std::snprintf(buf, size, "Amp");
  } else if (d == kDestPan) {
    std::snprintf(buf, size, "Pan");
  } else if (d == kDestPartLevel) {
    std::snprintf(buf, size, "Part Level");
  } else if (d >= kDestSourceP && d < kDestFilterP) {
    std::snprintf(buf, size, "Source P%d", d - kDestSourceP + 1);
  } else if (d >= kDestFilterP && d < kDestShaperP) {
    std::snprintf(buf, size, "Filter P%d", d - kDestFilterP + 1);
  } else if (d >= kDestShaperP && d < kDestPartFxP) {
    std::snprintf(buf, size, "Shaper P%d", d - kDestShaperP + 1);
  } else if (d >= kDestPartFxP && d < kDestPartFxP + kPartFxParamCount) {
    std::snprintf(buf, size, "Part FX P%d", d - kDestPartFxP + 1);
  } else if (d >= kDestModP && d < kNumDestinations) {
    std::snprintf(buf, size, "Mod %d P%d", (d - kDestModP) / kModParamCount + 1, (d - kDestModP) % kModParamCount + 1);
  } else {
    std::snprintf(buf, size, "?");
  }
}

}  // namespace eg13
