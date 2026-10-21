#pragma once

#include <cmath>
#include <cstddef>
#include <cstdio>

#include "engine/voice_engine.h"

namespace eg13 {

// 20 Hz .. 20 kHz, exponential: the VCV Ripples and Shelves frequency knobs (ripples.hpp:29-32,
// Ripples.cpp:36,75; shelves.hpp:28-29, Shelves.cpp:5-7,125-128). The Classic Filters and SVF
// cutoffs use the same curve so a modulation amount means the same sweep in every filter.
inline float knobHz(float normalized) { return 20.0f * std::pow(1000.0f, clamp01(normalized)); }

// Q 0.5 .. 40, exponential (0.5 * 80^p): Shelves' q_knob range (shelves.hpp:31-32), shared with
// the State Variable Filter's resonance.
inline float knobQ(float normalized) { return 0.5f * std::pow(80.0f, clamp01(normalized)); }

// Index of a stepped parameter with `steps` equal ranges over 0..1 (NaN -> 0).
inline int stepIndex(float normalized, int steps) {
  const int i = int(clamp01(normalized) * float(steps));
  return i < steps ? i : steps - 1;
}

inline void formatPercent01(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%d%%", int(std::lround(clamp01(v) * 100.0f)));
}

inline void formatHz(float hz, char* buf, size_t size) {
  if (hz < 999.5f) {
    std::snprintf(buf, size, "%d Hz", int(std::lround(hz)));
  } else {
    std::snprintf(buf, size, "%.2f kHz", double(hz) / 1000.0);
  }
}

inline void formatSemitones(float st, char* buf, size_t size) {
  std::snprintf(buf, size, "%+.1f st", double(st));
}

inline void formatDb(float db, char* buf, size_t size) { std::snprintf(buf, size, "%+.1f dB", double(db)); }

inline void formatKnobHz(float normalized, char* buf, size_t size) { formatHz(knobHz(normalized), buf, size); }

inline void formatKnobQ(float normalized, char* buf, size_t size) {
  std::snprintf(buf, size, "Q %.2f", double(knobQ(normalized)));
}

}  // namespace eg13
