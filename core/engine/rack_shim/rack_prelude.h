#pragma once

// What rack.hpp needs from outside itself: the standard and SSE headers, and Rack's random
// state. Kept apart so a TU built with Rack's flags can include it at file scope (one
// shared std, SSE and random state) and then rack.hpp inside its own namespace.

#include <algorithm>
#include <atomic>
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

namespace eg13_rack_shim {

constexpr uint64_t kRandomSeed = 0x9E3779B97F4A7C15ull;

// The random state for rack::random (rack.hpp), defined once, out of line and without Rack's
// flags, in rack_random.cpp: every copy of rack.hpp, flagged or not, shares it. Rack keeps one
// per thread; here it is one lock-free atomic for the process, read and written with relaxed
// order, because a thread_local is allocated (malloc) on a thread's first access on macOS, which
// the audio thread must not do. Threads drawing at once may draw the same number; Ripples only
// uses it for 1e-6 V of start-up noise.
std::atomic<uint64_t>& randomState();

}  // namespace eg13_rack_shim
