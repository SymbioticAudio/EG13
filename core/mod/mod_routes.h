#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/mod_engine.h"

namespace eg13 {

constexpr int kNumModSlots = 4;   // per part (spec §6)
constexpr int kNumRoutes = 12;    // per part (spec §6)
constexpr float kPitchRangeSemitones = 24.0f;  // a route amount of +-100 % into Pitch

// Matrix sources. Saved in projects: append only, never renumber. 17..31 are reserved for fixed
// sources added later; from kSrcMod on, the outputs of the part's mod slots:
// kSrcMod + slot * kMaxModOutputs + output.
enum ModSourceCode : uint16_t {
  kSrcNone = 0,
  kSrcVelocity = 1,
  kSrcReleaseVelocity = 2,
  kSrcNote = 3,            // keytrack: (note - 60) / 120
  kSrcGate = 4,
  kSrcRandom = 5,          // per voice, new on each note
  kSrcPressure = 6,        // poly aftertouch / MPE pressure
  kSrcSlide = 7,           // MPE slide (CC74)
  kSrcNoteBend = 8,        // per-note pitch bend / MPE Bend Range
  kSrcModWheel = 9,
  kSrcPitchBend = 10,
  kSrcChannelPressure = 11,
  kSrcCcA = 12,
  kSrcCcB = 13,
  kSrcCcC = 14,
  kSrcCcD = 15,
  kSrcTransportPhase = 16,  // position in the bar
  kSrcMod = 32,
};
constexpr int kNumSources = kSrcMod + kNumModSlots * kMaxModOutputs;  // 64
constexpr uint16_t modOutputSource(int slot, int output) {
  return uint16_t(kSrcMod + slot * kMaxModOutputs + output);
}

// Matrix destinations. Saved in projects: append only, never renumber. Each slot's range has
// room for 16 P's; Part FX P's are reserved for M5 (accepted, ignored until then).
enum ModDestinationCode : uint16_t {
  kDestNone = 0,
  kDestPitch = 1,      // semitones: amount x kPitchRangeSemitones
  kDestAmp = 2,
  kDestPan = 3,        // centre 0.5 in normalized space
  kDestPartLevel = 4,
  kDestSourceP = 16,   // + P index (0..15)
  kDestFilterP = 32,
  kDestShaperP = 48,
  kDestPartFxP = 64,
  kDestModP = 80,      // + slot * kModParamCount + P index
};
constexpr int kNumDestinations = kDestModP + kNumModSlots * kModParamCount;  // 144
constexpr uint16_t modParamDestination(int slot, int p) { return uint16_t(kDestModP + slot * kModParamCount + p); }

// Which evaluation a destination belongs to. Part FX P's are None until M5.
enum class DestinationKind { None, ModParam, Voice, Part };
DestinationKind destinationKind(uint16_t destination);

// A route (spec §6): source x amount x via (via None = 1) into destination. Source, destination
// and via are patch data; the amount is a host parameter.
struct Route {
  uint16_t source = kSrcNone;
  uint16_t destination = kDestNone;
  uint16_t via = kSrcNone;
};
bool operator==(const Route& a, const Route& b);
inline bool operator!=(const Route& a, const Route& b) { return !(a == b); }

struct RouteSet {
  Route route[kNumRoutes];
  float amount[kNumRoutes] = {};  // -1..1
};

// A route does something when its source and destination are known codes other than None and
// its via is None or known. Unknown codes (from a newer version) are kept but ignored.
bool routeActive(const Route& route);

// The sum of every active route into each destination of one kind. touched[d] is true when an
// active route targets d, even if it adds 0 (the Amp's "routed" test, spec §5.2).
struct RouteSums {
  float value[kNumDestinations];
  bool touched[kNumDestinations];
};
// source: kNumSources values. A non-finite contribution adds 0.
void sumRoutes(const RouteSet& routes, const float* source, DestinationKind kind, RouteSums* out);
// base + the routes into d, clamped to 0..1 (spec §6: routes add in normalized space and clamp).
inline float modulated(float base, const RouteSums& sums, uint16_t d) {
  return sums.touched[d] ? clamp01(base + sums.value[d]) : base;
}

void sourceName(uint16_t source, char* buf, size_t size);            // "Velocity", "Mod 2 Out 1"
void destinationName(uint16_t destination, char* buf, size_t size);  // "Pitch", "Filter P1"

}  // namespace eg13
