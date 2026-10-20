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

// Strongest spectral peak in [fLo, fHi] Hz over [begin, end): Hann window, Goertzel at a 1 Hz
// step, then a 0.01 Hz scan of +-1 Hz around the maximum.
inline double peakFrequency(const std::vector<float>& x, double rate, size_t begin, size_t end,
                            double fLo, double fHi) {
  const size_t count = end > begin ? end - begin : 0;
  std::vector<double> w(count);
  for (size_t i = 0; i < count; ++i) {
    const double hann = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * double(i) / double(count));
    w[i] = double(x[begin + i]) * hann;
  }
  auto magnitude = [&](double f) {
    const double coeff = 2.0 * std::cos(2.0 * 3.14159265358979323846 * f / rate);
    double s1 = 0.0, s2 = 0.0;
    for (size_t i = 0; i < count; ++i) {
      const double s0 = w[i] + coeff * s1 - s2;
      s2 = s1;
      s1 = s0;
    }
    return s1 * s1 + s2 * s2 - coeff * s1 * s2;
  };
  double best = -1.0, bestF = fLo;
  for (double f = fLo; f <= fHi; f += 1.0) {
    const double m = magnitude(f);
    if (m > best) { best = m; bestF = f; }
  }
  const double center = bestF;
  for (int k = -100; k <= 100; ++k) {
    const double f = center + 0.01 * k;
    const double m = magnitude(f);
    if (m > best) { best = m; bestF = f; }
  }
  return bestF;
}

inline double rms(const std::vector<float>& x, size_t start, size_t end) {
  double sum = 0.0;
  for (size_t i = start; i < end; ++i) sum += double(x[i]) * double(x[i]);
  return end > start ? std::sqrt(sum / double(end - start)) : 0.0;
}

}  // namespace eg13::test
