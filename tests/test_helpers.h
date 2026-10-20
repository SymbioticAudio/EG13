#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

#include "engine/voice_engine.h"

namespace eg13::test {

inline ParamValues defaultParams(const EngineInfo& info) {
  ParamValues p;
  for (int i = 0; i < info.paramCount; ++i) p.p[i] = info.params[i].defaultValue;
  return p;
}

// Fundamental from rising zero crossings (linearly interpolated) in [start, end).
inline double estimateFrequency(const std::vector<float>& x, double rate,
                                size_t start, size_t end) {
  double first = -1.0;
  double last = -1.0;
  int crossings = 0;
  for (size_t i = start + 1; i < end; ++i) {
    if (x[i - 1] < 0.0f && x[i] >= 0.0f) {
      const double t = double(i - 1) + double(-x[i - 1]) / double(x[i] - x[i - 1]);
      if (first < 0.0) first = t;
      last = t;
      ++crossings;
    }
  }
  if (crossings < 2) return 0.0;
  return double(crossings - 1) * rate / (last - first);
}

inline double rms(const std::vector<float>& x, size_t start, size_t end) {
  double sum = 0.0;
  for (size_t i = start; i < end; ++i) sum += double(x[i]) * double(x[i]);
  return end > start ? std::sqrt(sum / double(end - start)) : 0.0;
}

}  // namespace eg13::test
