#pragma once

// The parts of the VCV Rack SDK that AudibleInstruments' Ripples and Shelves models use
// (src/Ripples/ripples.hpp, src/Shelves/shelves.hpp, their aafilter.hpp and sos.hpp), so those
// models compile unmodified without Rack. Each definition follows Rack v2 (VCVRack/Rack
// 061ccf6, GPL-3.0-or-later): file:line cited per item. Only Rack's exp/log approximations
// (sse_mathfun) are replaced by <cmath>; the models only use them for CV levels.

#include <algorithm>
#include <cmath>
#include <cstdint>

#if defined(__SSE2__) || defined(_M_X64)
#include <xmmintrin.h>
#else
#ifndef SIMDE_ENABLE_NATIVE_ALIASES
#define SIMDE_ENABLE_NATIVE_ALIASES  // _mm_* names on arm64, as Rack builds with simde there
#endif
#include "simde/x86/sse.h"
#endif

namespace rack {

namespace math {

// include/math.hpp:106-108
inline float clamp(float x, float a = 0.f, float b = 1.f) { return std::fmax(std::fmin(x, b), a); }

// include/math.hpp:151-153
inline float rescale(float x, float xMin, float xMax, float yMin, float yMax) {
  return yMin + (x - xMin) / (xMax - xMin) * (yMax - yMin);
}

}  // namespace math

namespace simd {

// include/simd/Vector.hpp:34-94: four floats in one SSE register.
struct float_4 {
  union {
    __m128 v;
    float s[4];
  };
  float_4() = default;  // trivial, so float_4 can sit in the models' unions
  float_4(__m128 x) : v(x) {}
  float_4(float x) { v = _mm_set1_ps(x); }
  float_4(float x1, float x2, float x3, float x4) { v = _mm_setr_ps(x1, x2, x3, x4); }
  float& operator[](int i) { return s[i]; }
  const float& operator[](int i) const { return s[i]; }
};

// include/simd/Vector.hpp operators (non-member so a float converts on either side).
inline float_4 operator+(float_4 a, float_4 b) { return _mm_add_ps(a.v, b.v); }
inline float_4 operator-(float_4 a, float_4 b) { return _mm_sub_ps(a.v, b.v); }
inline float_4 operator*(float_4 a, float_4 b) { return _mm_mul_ps(a.v, b.v); }
inline float_4 operator/(float_4 a, float_4 b) { return _mm_div_ps(a.v, b.v); }
inline float_4 operator-(float_4 a) { return _mm_sub_ps(_mm_setzero_ps(), a.v); }
inline float_4& operator+=(float_4& a, float_4 b) { return a = a + b; }
inline float_4& operator-=(float_4& a, float_4 b) { return a = a - b; }
inline float_4& operator*=(float_4& a, float_4 b) { return a = a * b; }
inline float_4& operator/=(float_4& a, float_4 b) { return a = a / b; }

inline float_4 fmin(float_4 a, float_4 b) { return _mm_min_ps(a.v, b.v); }
inline float_4 fmax(float_4 a, float_4 b) { return _mm_max_ps(a.v, b.v); }

// include/simd/functions.hpp:249-259
using math::clamp;
inline float_4 clamp(float_4 x, float_4 a = 0.f, float_4 b = 1.f) { return fmin(fmax(x, a), b); }
using math::rescale;
inline float_4 rescale(float_4 x, float_4 xMin, float_4 xMax, float_4 yMin, float_4 yMax) {
  return yMin + (x - xMin) / (xMax - xMin) * (yMax - yMin);
}

// include/simd/functions.hpp:231-233: exp(b * log(a)), per lane with <cmath>.
inline float_4 pow(float a, float_4 b) {
  const float l = std::log(a);
  return float_4(std::exp(b[0] * l), std::exp(b[1] * l), std::exp(b[2] * l), std::exp(b[3] * l));
}

}  // namespace simd

namespace dsp {

constexpr float kPi = 3.14159265358979f;  // Rack uses M_PI

// include/dsp/filter.hpp:13-52
template <typename T = float>
struct TRCFilter {
  T c = 0.f;
  T xstate[1];
  T ystate[1];
  TRCFilter() { reset(); }
  void reset() {
    xstate[0] = 0.f;
    ystate[0] = 0.f;
  }
  void setCutoff(T r) { c = 2.f / r; }
  void setCutoffFreq(T f) { setCutoff(2.f * kPi * f); }
  void process(T x) {
    T y = (x + xstate[0] - ystate[0] * (1 - c)) / (1 + c);
    xstate[0] = x;
    ystate[0] = y;
  }
  T lowpass() { return ystate[0]; }
  T highpass() { return xstate[0] - ystate[0]; }
};
typedef TRCFilter<> RCFilter;

// include/dsp/filter.hpp:132-155
template <typename T = float>
struct TSlewLimiter {
  T out = 0.f;
  T rise = 0.f;
  T fall = 0.f;
  void reset() { out = 0.f; }
  void setRiseFall(T r, T f) {
    rise = r;
    fall = f;
  }
  T process(T deltaTime, T in) {
    out = simd::clamp(in, out - fall * deltaTime, out + rise * deltaTime);
    return out;
  }
};
typedef TSlewLimiter<> SlewLimiter;

}  // namespace dsp

namespace random {

// Rack's random::uniform() (include/random.hpp:125-126) draws from a per-thread xoroshiro128+.
// Ripples only uses it for 1e-6 V of noise to start self-oscillation (ripples.hpp:193-194), so
// a per-thread xorshift64 serves; seed() makes renders repeatable in tests.
inline thread_local uint64_t tState = 0x9E3779B97F4A7C15ull;
inline void seed(uint64_t s) { tState = s != 0 ? s : 0x9E3779B97F4A7C15ull; }
inline float uniform() {
  tState ^= tState << 13;
  tState ^= tState >> 7;
  tState ^= tState << 17;
  return float(tState >> 40) * (1.0f / 16777216.0f);
}

}  // namespace random

}  // namespace rack
