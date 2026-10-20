# EG13 M1 (Synth Architecture) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the M0 mono skeleton into the spec's bi-timbral architecture: two parts with 8 voices each, engines running at their native rate through a low-latency resampler, Single/Layer/Split/Multi modes, the full part and master parameter set, state v2 with atomic loading, the audio input path (sidechain, Dry level, Always-on trigger) and latency reporting.

**Architecture:** `/core` gains a `NativeRateWrapper` (r8brain) around each voice's source engine, a pure-logic `VoiceAllocator`, `Voice` (glide, bend, silence detection), `Part` (8 voices + settings) and a rebuilt `Synth` that routes MIDI to parts by mode, mixes to a main and an optional second bus, adds the latency-compensated Dry input, and applies parameter snapshots atomically. The CLAP plugin exposes the new parameters (stepped where needed), a sidechain input and second output on the instrument, and the latency extension.

**Tech Stack:** C++17, CMake + Ninja, Catch2 v3, r8brain-free-src 7.6 (MIT, header-only), CLAP 1.2.10, clap-wrapper v0.16.0, upstream pichenettes/eurorack.

**Spec:** `docs/superpowers/specs/2026-10-20-eg13-design.md` (milestone M1, §11; §2.3 audio input; §3.2 native rate; §5 voice and part; §7 parameters and state; §9 error handling).

## Global Constraints

- EG13 is GPL-3.0. Never use "Mutable Instruments" in product name, UI or marketing. Plaits is shown as "Macro Oscillator".
- macOS 14+ deployment target, universal binary (arm64 + x86_64). C++17.
- `/core` has no OS, plugin or UI dependencies and must build and test on macOS and Linux.
- The audio thread never allocates, locks or waits. Allocation happens only in constructors and `prepare`. Tests use `eg13::test::ScopedAllocationCounter` (Task 2) to prove it.
- `third_party/eurorack` stays unmodified. Unavoidable changes live in `third_party/eurorack-patches/` as shadow files, each documented in its README.
- Every translation unit that includes eurorack headers is compiled with `TEST` defined.
- Parameter IDs are stable and never renumbered: `id = (part << 16) | (slot << 8) | index`. Master parameters use part `0xFF`. M0's IDs (Part A Source P1–P8 = 257–264, master level = 16711680) keep their meaning and range.
- Decided 2026-10-20: the Macro Oscillator's Model stays its automatable P1; part settings that the spec lists as patch data (MIDI channel, key range, voice mode, voice count, trigger, pitch-bend range, Mono, output bus) are also stepped host parameters, as are master mode and split note.
- Resampler: r8brain `CDSPResampler24` with a 30% transition band (`kTransitionBandPercent = 30.0`), giving at most 3 ms latency at 44.1–192 kHz host rates.
- The plugin display name is the CMake cache variable `EG13_DISPLAY_NAME`. Permanent identifiers are unchanged: CLAP IDs `com.symbioticaudio.eg13` / `com.symbioticaudio.eg13.fx`; AU manufacturer `Symb`; subtypes `EG13` (`aumu`) and `EGfx` (`aumf`).
- Commits are authored by `Westlee Latta <symbioticaudio@proton.me>` (repo git config) and end with:
  ```
  Co-Authored-By: <the model that wrote the change> <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01BfbDAN2RSSY1ZShBua2VNM
  ```

## Scope notes (M1 decisions where the spec is silent or later milestones finish the job)

- **Amp:** there is no modulation matrix until M4, so the Amp stage is unity and the source's own envelope (Plaits' LPG) shapes the note. A voice is freed when its gate is off and its output stays below −90 dBFS for 50 ms (spec §5.2). M4 adds the gate-following Amp and routes.
- **Modes are a routing policy on top of each part's own filters.** A note reaches a part only if the part's MIDI channel and key range accept it, and then: Single sends it to Part A only (Part B is also muted); Layer to both; Split to A below the split note and B from it upward; Multi to whichever parts' channel filters accept it.
- **Dry level default:** 1.0 for EG13 FX (so it still passes audio through, as in M0) and 0.0 for the instrument. M2 adds the Audio Input source and the FX init patch from spec §6, and may revisit this default.
- **Input conversion to the native rate** (needed by the Audio Input source and the Resonator/Modal exciters) arrives with its first consumer in M2. In M1 the Input feeds only the Dry path.
- **Event timing:** notes and parameters take effect at the engine's native block boundary (12 samples at 48 kHz for Plaits, 0.25 ms). This matches the hardware's control rate (spec §3.2) and is intentional.
- **Engine selector parameters** are not registered yet because there is only one source engine. M2 must register them with a fixed integer range so adding engines never changes their meaning.
- **Sustain pedal (CC 64)** is supported: a reasonable player expects it from a polysynth.

## Review Focus

1. **Playing live:** a note must sound within about 3 ms at common host rates (44.1, 48, 88.2, 96, 192 kHz) and stay in tune. Pinned in Task 3 ("latency is reported and at most 3 ms", "keeps Plaits in tune at common host rates").
2. **Changing voice count, voice mode or trigger while notes are held** must not leave stuck or silent voices. Pinned in Task 4 ("configure releases sounding notes") and Task 7 ("voice count change while notes are held releases them").
3. **A host loading a project or preset while audio is running** must switch every parameter at once, with no half-applied state, and `get_value` must report the new values immediately. Pinned in Task 8 ("loadParams while processing is applied whole at the next block") and Task 9 ("load while processing is reflected immediately").
4. **Unconnected sidechain or Bus 2:** the instrument must play normally when a host leaves the sidechain or the second output disconnected (null pointers, silent buffers or zero channels). Pinned in Task 8 ("Bus 2 falls back to the main output when not connected", "Instrument ignores input when Dry is 0") and Task 10 (plugin input/output guards, host checks).
5. **Sustain pedal and voice stealing together:** notes released under the pedal keep sounding until pedal up, and stealing prefers released voices over held or sustained ones. Pinned in Task 4 ("sustain pedal holds released notes", "stealing prefers released voices").

## File Structure

```
third_party/r8brain-free-src            submodule @ cb2abb9977efe2471979b380ed95daa56ab4fdb9 (version 7.6, MIT)
third_party/eurorack-patches/README.md  why each shadow file exists
third_party/eurorack-patches/stmlib/utils/random.h|.cc   thread-local RNG state
cmake/eurorack.cmake                    patches dir first on the include path; patched random.cc
cmake/r8brain.cmake                     eg13_r8brain INTERFACE target
core/util/smoothed_value.h              one-pole parameter smoother
core/engine/voice_engine.h              + setNote()
core/engine/plaits_engine.h             + setNote()
core/engine/native_rate_wrapper.h|.cpp  native-rate engine runner + r8brain conversion
core/voice/voice_allocator.h|.cpp       poly/mono/legato allocation, stealing, sustain
core/voice/voice.h|.cpp                 one voice: source wrapper, glide, bend, silence detection
core/voice/source_factory.h|.cpp        SourceSpec + makePlaitsSource()
core/synth/plugin_kind.h                PluginKind (moved out of synth.h)
core/synth/param_table.h|.cpp           v2 table: 48 parameters with ranges, stepping, formats
core/part/part.h|.cpp                   one part: 8 voices, settings, always-on, level/pan/mono
core/synth/synth.h|.cpp                 v2: routing, modes, buses, Dry, latency, snapshots
core/state/state.h|.cpp                 v2: StateStore with schema 2 and preserved unknown data
tests/alloc_counter.h|.cpp              ScopedAllocationCounter (global operator new hook)
tests/test_engines.h                    ImpulseEngine, SineEngine
tests/test_*.cpp                        new and updated tests (named per task)
plugin/src/eg13_plugin.cpp              v2 ports, params, events, latency, state store
plugin/CMakeLists.txt                   AU plist regenerates when the CLAP changes
README.md                               Logic restart note, latency note
```

---
### Task 1: Thread-local stmlib random state

Upstream `stmlib::Random` keeps one global `static uint32_t rng_state_`. Several EG13 instances processed on different host threads race on it (undefined behaviour), and voices on one thread perturb each other's noise. A shadow header makes the state thread-local without editing `third_party/eurorack`.

**Files:**
- Create: `third_party/eurorack-patches/README.md`, `third_party/eurorack-patches/stmlib/utils/random.h`, `third_party/eurorack-patches/stmlib/utils/random.cc`, `tests/test_stmlib_random.cpp`
- Modify: `cmake/eurorack.cmake`, `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `stmlib::Random` with the upstream API (`state`, `Seed`, `GetWord`, `GetSample`, `GetFloat`) and per-thread state initialised to `0x21`. `eg13_tests` links `Threads::Threads`.

- [ ] **Step 1: Write the failing test `tests/test_stmlib_random.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <thread>

#include "stmlib/utils/random.h"

TEST_CASE("stmlib::Random state is per thread") {
  stmlib::Random::Seed(1234);
  const uint32_t expected = stmlib::Random::GetWord();

  stmlib::Random::Seed(1234);
  std::thread other([] {
    stmlib::Random::Seed(999);
    for (int i = 0; i < 100; ++i) stmlib::Random::GetWord();
  });
  other.join();
  REQUIRE(stmlib::Random::GetWord() == expected);
}

TEST_CASE("stmlib::Random starts from the upstream seed on every thread") {
  uint32_t initial = 0;
  std::thread fresh([&initial] { initial = stmlib::Random::state(); });
  fresh.join();
  REQUIRE(initial == 0x21u);
}
```

- [ ] **Step 2: Link threads in `tests/CMakeLists.txt`**

Replace the `target_link_libraries` line with:

```cmake
find_package(Threads REQUIRED)
target_link_libraries(eg13_tests PRIVATE eg13_core eg13_eurorack Catch2::Catch2WithMain Threads::Threads)
```

- [ ] **Step 3: Run to verify it fails**

Run: `cmake --build --preset mac --target eg13_tests && ctest --preset mac -R "per thread"`
Expected: FAIL — the second thread's `Seed(999)` changes the shared state, so `GetWord()` differs from `expected`.

- [ ] **Step 4: Write `third_party/eurorack-patches/stmlib/utils/random.h`**

```cpp
// Copyright 2012 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Random number generator.
//
// EG13 patch: identical to upstream stmlib/utils/random.h except that the
// generator state is thread_local (see third_party/eurorack-patches/README.md).

#ifndef STMLIB_UTILS_RANDOM_H_
#define STMLIB_UTILS_RANDOM_H_

#include "stmlib/stmlib.h"

namespace stmlib {

class Random {
 public:
  static inline uint32_t state() { return rng_state_; }

  static inline void Seed(uint32_t seed) {
    rng_state_ = seed;
  }

  static inline uint32_t GetWord() {
    rng_state_ = rng_state_ * 1664525L + 1013904223L;
    return state();
  }
  
  static inline int16_t GetSample() {
    return static_cast<int16_t>(GetWord() >> 16);
  }

  static inline float GetFloat() {
    return static_cast<float>(GetWord()) / 4294967296.0f;
  }

 private:
  static thread_local uint32_t rng_state_;

  DISALLOW_COPY_AND_ASSIGN(Random);
};

}  // namespace stmlib

#endif  // STMLIB_UTILS_RANDOM_H_
```

- [ ] **Step 5: Write `third_party/eurorack-patches/stmlib/utils/random.cc`**

```cpp
// Copyright 2012 Emilie Gillet. MIT licence (see random.h).
//
// EG13 patch: thread_local definition of the generator state.

#include "stmlib/utils/random.h"

namespace stmlib {

/* static */
thread_local uint32_t Random::rng_state_ = 0x21;

}  // namespace stmlib
```

- [ ] **Step 6: Write `third_party/eurorack-patches/README.md`**

```markdown
# Eurorack patches

`third_party/eurorack` is compiled unmodified. When an upstream file cannot be
used as-is, EG13 ships a shadow copy here. This directory comes first on the
include path, so `#include "stmlib/..."` finds the shadow file, and the
build compiles the shadow `.cc` instead of the upstream one.

| File | Upstream | Change | Why |
|---|---|---|---|
| `stmlib/utils/random.h`, `random.cc` | `stmlib/utils/random.{h,cc}` @ eurorack f4cfa76 | `rng_state_` is `thread_local` | Upstream keeps one global generator state. EG13 instances processed on different host threads would race on it (undefined behaviour). |
```

- [ ] **Step 7: Use the patches in `cmake/eurorack.cmake`**

Replace `${EURORACK_DIR}/stmlib/utils/random.cc` in the `add_library(eg13_eurorack ...)` source list with `${EG13_EURORACK_PATCHES}/stmlib/utils/random.cc`, define the variable at the top of the file, and put the patches directory before the upstream directory:

```cmake
set(EURORACK_DIR ${CMAKE_SOURCE_DIR}/third_party/eurorack)
set(EG13_EURORACK_PATCHES ${CMAKE_SOURCE_DIR}/third_party/eurorack-patches)
```

```cmake
target_include_directories(eg13_eurorack SYSTEM BEFORE PUBLIC ${EG13_EURORACK_PATCHES})
target_include_directories(eg13_eurorack SYSTEM PUBLIC ${EURORACK_DIR})
```

(The first line must come before the existing `target_include_directories(eg13_eurorack SYSTEM PUBLIC ${EURORACK_DIR})`; `BEFORE` guarantees the order either way.)

- [ ] **Step 8: Run the tests**

Run: `cmake --build --preset mac --target eg13_tests && ctest --preset mac`
Expected: all tests pass, including both new cases and the bit-exact Plaits test (which runs on one thread and is unaffected).

- [ ] **Step 9: Commit**

```bash
git add third_party/eurorack-patches cmake/eurorack.cmake tests/CMakeLists.txt tests/test_stmlib_random.cpp
git commit -m "Make stmlib random state thread-local via eurorack-patches"
```
(Add the two trailer lines from Global Constraints to every commit message.)

---

### Task 2: Allocation counter and parameter smoother

**Files:**
- Create: `tests/alloc_counter.h`, `tests/alloc_counter.cpp`, `tests/test_alloc_counter.cpp`, `core/util/smoothed_value.h`, `tests/test_smoothed_value.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `namespace eg13::test { class ScopedAllocationCounter { public: ScopedAllocationCounter(); ~ScopedAllocationCounter(); size_t count() const; }; }` — counts `operator new` calls on the current thread while alive.
  - `namespace eg13 { class SmoothedValue { void prepare(double sampleRate, double timeSeconds); void reset(float value); void setTarget(float value); float next(); float current() const; float target() const; }; }` — one-pole smoothing that snaps to the target when within 1e-6.

- [ ] **Step 1: Write the failing tests**

`tests/test_alloc_counter.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

#include "alloc_counter.h"

TEST_CASE("ScopedAllocationCounter counts allocations on this thread") {
  eg13::test::ScopedAllocationCounter counter;
  auto p = std::make_unique<int>(3);
  std::vector<int> v(10);
  REQUIRE(counter.count() == 2);
}

TEST_CASE("ScopedAllocationCounter sees nothing when nothing allocates") {
  int stack[4] = {1, 2, 3, 4};
  eg13::test::ScopedAllocationCounter counter;
  int sum = 0;
  for (int x : stack) sum += x;
  REQUIRE(sum == 10);
  REQUIRE(counter.count() == 0);
}
```

`tests/test_smoothed_value.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "util/smoothed_value.h"

using eg13::SmoothedValue;

TEST_CASE("SmoothedValue reset jumps immediately") {
  SmoothedValue s;
  s.prepare(48000.0, 0.01);
  s.reset(0.5f);
  REQUIRE(s.current() == 0.5f);
  REQUIRE(s.next() == 0.5f);
}

TEST_CASE("SmoothedValue moves monotonically and reaches the target exactly") {
  SmoothedValue s;
  s.prepare(48000.0, 0.01);
  s.reset(0.0f);
  s.setTarget(1.0f);
  float previous = 0.0f;
  for (int i = 0; i < 480; ++i) {  // one time constant
    const float v = s.next();
    REQUIRE(v >= previous);
    REQUIRE(v <= 1.0f);
    previous = v;
  }
  REQUIRE(previous > 0.6f);
  REQUIRE(previous < 0.7f);
  for (int i = 0; i < 48000; ++i) s.next();
  REQUIRE(s.current() == 1.0f);
}

TEST_CASE("SmoothedValue with zero time follows the target at once") {
  SmoothedValue s;
  s.prepare(48000.0, 0.0);
  s.reset(0.0f);
  s.setTarget(0.25f);
  REQUIRE(s.next() == 0.25f);
}
```

- [ ] **Step 2: Add the counter to `tests/CMakeLists.txt`**

Change the `add_executable` line to:

```cmake
add_executable(eg13_tests ${EG13_TEST_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/alloc_counter.cpp)
```

- [ ] **Step 3: Run to verify it fails**

Run: `cmake --build --preset mac --target eg13_tests`
Expected: compile error, `'alloc_counter.h' file not found`.

- [ ] **Step 4: Write `tests/alloc_counter.h`**

```cpp
#pragma once

#include <cstddef>

namespace eg13::test {

// Counts global operator new calls made on the current thread while alive.
// Used to prove audio-thread code paths never allocate.
class ScopedAllocationCounter {
 public:
  ScopedAllocationCounter();
  ~ScopedAllocationCounter();
  ScopedAllocationCounter(const ScopedAllocationCounter&) = delete;
  ScopedAllocationCounter& operator=(const ScopedAllocationCounter&) = delete;

  size_t count() const;
};

}  // namespace eg13::test
```

- [ ] **Step 5: Write `tests/alloc_counter.cpp`**

```cpp
#include "alloc_counter.h"

#include <cstdlib>
#include <new>

namespace {
thread_local bool tCounting = false;
thread_local size_t tCount = 0;
}  // namespace

namespace eg13::test {

ScopedAllocationCounter::ScopedAllocationCounter() {
  tCount = 0;
  tCounting = true;
}

ScopedAllocationCounter::~ScopedAllocationCounter() { tCounting = false; }

size_t ScopedAllocationCounter::count() const { return tCount; }

}  // namespace eg13::test

void* operator new(std::size_t size) {
  if (tCounting) ++tCount;
  if (void* p = std::malloc(size ? size : 1)) return p;
  throw std::bad_alloc();
}

void* operator new[](std::size_t size) { return operator new(size); }

void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
```

- [ ] **Step 6: Write `core/util/smoothed_value.h`**

```cpp
#pragma once

#include <cmath>

namespace eg13 {

// One-pole parameter smoother. Snaps to the target once within 1e-6 so a
// level of zero becomes exactly zero.
class SmoothedValue {
 public:
  void prepare(double sampleRate, double timeSeconds) {
    coeff_ = timeSeconds > 0.0 ? float(std::exp(-1.0 / (timeSeconds * sampleRate))) : 0.0f;
  }

  void reset(float value) {
    current_ = value;
    target_ = value;
  }

  void setTarget(float value) { target_ = value; }

  float next() {
    current_ = target_ + (current_ - target_) * coeff_;
    if (std::fabs(current_ - target_) < 1e-6f) current_ = target_;
    return current_;
  }

  float current() const { return current_; }
  float target() const { return target_; }

 private:
  float coeff_ = 0.0f;
  float current_ = 0.0f;
  float target_ = 0.0f;
};

}  // namespace eg13
```

- [ ] **Step 7: Run the tests**

Run: `cmake --build --preset mac --target eg13_tests && ctest --preset mac`
Expected: all tests pass.

- [ ] **Step 8: Commit**

```bash
git add tests/alloc_counter.h tests/alloc_counter.cpp tests/test_alloc_counter.cpp core/util/smoothed_value.h tests/test_smoothed_value.cpp tests/CMakeLists.txt
git commit -m "Add allocation counter for tests and a parameter smoother"
```

---

### Task 3: Native-rate wrapper with r8brain

**Files:**
- Create: `cmake/r8brain.cmake`, `core/engine/native_rate_wrapper.h`, `core/engine/native_rate_wrapper.cpp`, `tests/test_engines.h`, `tests/test_native_rate_wrapper.cpp`
- Create (git): submodule `third_party/r8brain-free-src`
- Modify: `CMakeLists.txt`, `core/CMakeLists.txt`, `core/engine/voice_engine.h`, `core/engine/plaits_engine.h`, `tests/test_plaits_engine.cpp`

**Interfaces:**
- Consumes: `VoiceEngine`, `ParamValues` (M0); `PlaitsEngine`; `ScopedAllocationCounter` (Task 2); `test::defaultParams`, `test::estimateFrequency` (M0 `tests/test_helpers.h`).
- Produces:
  - `VoiceEngine::setNote(float note)` — pure virtual; changes pitch without retriggering. `PlaitsEngine::setNote` sets the note used from the next native block.
  - `class eg13::NativeRateWrapper final : public VoiceEngine` with `NativeRateWrapper(std::unique_ptr<VoiceEngine> inner, double nativeRate, int nativeBlock)`, `static constexpr double kTransitionBandPercent = 30.0`, `int latencySamples() const` (host-rate samples, valid after `prepare`). It forwards notes to the inner engine, runs it at `nativeRate` in `nativeBlock`-sample blocks, and converts to the host rate. At the native rate it is a bit-exact pass-through with latency 0. M1 ignores the `in` argument (input conversion arrives in M2).
  - `tests/test_engines.h`: `eg13::test::ImpulseEngine` (one 1.0 sample after each `noteOn`) and `eg13::test::SineEngine` (0.5-amplitude sine at the note's frequency while the gate is on, silent otherwise).
  - CMake target `eg13_r8brain` (INTERFACE, SYSTEM include of the r8brain sources, links `Threads::Threads`).

- [ ] **Step 1: Add the r8brain submodule**

```bash
git submodule add https://github.com/avaneev/r8brain-free-src.git third_party/r8brain-free-src
git -C third_party/r8brain-free-src checkout cb2abb9977efe2471979b380ed95daa56ab4fdb9
ls third_party/r8brain-free-src/CDSPResampler.h
```
Expected: the header is listed. (If upstream keeps headers under `src/`, use that directory in Step 2 instead and note it in the commit.)

- [ ] **Step 2: Write `cmake/r8brain.cmake` and include it**

```cmake
# r8brain-free-src 7.6 (MIT): header-only sample-rate converter.
find_package(Threads REQUIRED)
add_library(eg13_r8brain INTERFACE)
target_include_directories(eg13_r8brain SYSTEM INTERFACE ${CMAKE_SOURCE_DIR}/third_party/r8brain-free-src)
target_link_libraries(eg13_r8brain INTERFACE Threads::Threads)
```

In the root `CMakeLists.txt`, add `include(cmake/r8brain.cmake)` on the line after `include(cmake/eurorack.cmake)`.

- [ ] **Step 3: Write `tests/test_engines.h`**

```cpp
#pragma once

#include <cmath>

#include "engine/voice_engine.h"

namespace eg13::test {

// Emits one 1.0 sample at the first rendered sample after each noteOn.
class ImpulseEngine final : public VoiceEngine {
 public:
  void prepare(double, int) override {}
  void reset() override { pending_ = false; }
  void noteOn(float, float) override { pending_ = true; }
  void noteOff() override {}
  void setNote(float) override {}
  void process(const float* const*, float* const* out, const ParamValues&, int n) override {
    for (int i = 0; i < n; ++i) {
      const float v = pending_ ? 1.0f : 0.0f;
      pending_ = false;
      out[0][i] = v;
      out[1][i] = v;
    }
  }
  const EngineInfo& info() const override {
    static const EngineInfo kInfo = {"impulse", "Impulse", 0, nullptr};
    return kInfo;
  }

 private:
  bool pending_ = false;
};

// 0.5-amplitude sine at the note's frequency while the gate is on; silent otherwise.
class SineEngine final : public VoiceEngine {
 public:
  void prepare(double rate, int) override { rate_ = rate; }
  void reset() override {
    gate_ = false;
    phase_ = 0.0;
  }
  void noteOn(float note, float) override {
    note_ = note;
    gate_ = true;
  }
  void noteOff() override { gate_ = false; }
  void setNote(float note) override { note_ = note; }
  void process(const float* const*, float* const* out, const ParamValues&, int n) override {
    constexpr double kTwoPi = 6.283185307179586;
    const double increment = 440.0 * std::pow(2.0, (double(note_) - 69.0) / 12.0) / rate_;
    for (int i = 0; i < n; ++i) {
      const float v = gate_ ? 0.5f * float(std::sin(kTwoPi * phase_)) : 0.0f;
      phase_ += increment;
      phase_ -= std::floor(phase_);
      out[0][i] = v;
      out[1][i] = v;
    }
  }
  const EngineInfo& info() const override {
    static const EngineInfo kInfo = {"sine", "Sine", 0, nullptr};
    return kInfo;
  }

 private:
  double rate_ = 48000.0;
  double phase_ = 0.0;
  float note_ = 60.0f;
  bool gate_ = false;
};

}  // namespace eg13::test
```

- [ ] **Step 4: Write the failing tests `tests/test_native_rate_wrapper.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <vector>

#include "alloc_counter.h"
#include "engine/native_rate_wrapper.h"
#include "engine/plaits_engine.h"
#include "test_engines.h"
#include "test_helpers.h"

using namespace eg13;

namespace {

std::vector<float> render(VoiceEngine& engine, const ParamValues& p, int n, int chunk) {
  std::vector<float> left(static_cast<size_t>(n)), right(static_cast<size_t>(n));
  for (int done = 0; done < n;) {
    const int count = std::min(chunk, n - done);
    float* out[2] = {left.data() + done, right.data() + done};
    engine.process(nullptr, out, p, count);
    done += count;
  }
  return left;
}

std::unique_ptr<NativeRateWrapper> wrappedPlaits() {
  return std::make_unique<NativeRateWrapper>(std::make_unique<PlaitsEngine>(), 48000.0, 12);
}

}  // namespace

TEST_CASE("NativeRateWrapper is a bit-exact pass-through at the native rate") {
  auto wrapped = wrappedPlaits();
  PlaitsEngine direct;
  wrapped->prepare(48000.0, 512);
  direct.prepare(48000.0, 512);
  REQUIRE(wrapped->latencySamples() == 0);
  const auto p = test::defaultParams(PlaitsEngine::staticInfo());
  wrapped->noteOn(60.0f, 1.0f);
  direct.noteOn(60.0f, 1.0f);
  REQUIRE(render(*wrapped, p, 4800, 256) == render(direct, p, 4800, 256));
}

TEST_CASE("NativeRateWrapper keeps Plaits in tune at common host rates") {
  const auto p = test::defaultParams(PlaitsEngine::staticInfo());
  for (double rate : {44100.0, 88200.0, 96000.0, 192000.0}) {
    auto wrapped = wrappedPlaits();
    wrapped->prepare(rate, 512);
    wrapped->noteOn(69.0f, 1.0f);
    const int n = int(rate * 1.5);
    const auto out = render(*wrapped, p, n, 512);
    const double f = test::estimateFrequency(out, rate, size_t(rate * 0.5), size_t(n));
    INFO("rate " << rate << " measured " << f);
    REQUIRE_THAT(f, Catch::Matchers::WithinRel(440.0, 0.001));
  }
}

TEST_CASE("NativeRateWrapper latency is reported and at most 3 ms") {
  for (double rate : {44100.0, 96000.0, 192000.0}) {
    NativeRateWrapper wrapped(std::make_unique<test::ImpulseEngine>(), 48000.0, 12);
    wrapped.prepare(rate, 512);
    const int latency = wrapped.latencySamples();
    INFO("rate " << rate << " latency " << latency);
    REQUIRE(latency > 0);
    REQUIRE(double(latency) / rate <= 0.003);

    const ParamValues p;
    render(wrapped, p, 4000, 512);  // run past the converter's start-up
    wrapped.noteOn(60.0f, 1.0f);
    const auto out = render(wrapped, p, 4000, 512);
    const auto peak = std::max_element(out.begin(), out.end(), [](float a, float b) {
      return std::fabs(a) < std::fabs(b);
    });
    const int delay = int(peak - out.begin());
    const int tolerance = int(std::ceil(12.0 * rate / 48000.0)) + 2;  // one native block
    INFO("delay " << delay);
    REQUIRE(std::abs(delay - latency) <= tolerance);
  }
}

TEST_CASE("NativeRateWrapper output is independent of host block size") {
  const auto p = test::defaultParams(PlaitsEngine::staticInfo());
  auto a = wrappedPlaits();
  auto b = wrappedPlaits();
  auto c = wrappedPlaits();
  for (auto* w : {a.get(), b.get(), c.get()}) {
    w->prepare(44100.0, 4096);
    w->noteOn(60.0f, 1.0f);
  }
  const auto ra = render(*a, p, 9000, 4096);
  REQUIRE(ra == render(*b, p, 9000, 1));
  REQUIRE(ra == render(*c, p, 9000, 7));
}

TEST_CASE("NativeRateWrapper process does not allocate") {
  auto wrapped = wrappedPlaits();
  wrapped->prepare(44100.0, 512);
  const auto p = test::defaultParams(PlaitsEngine::staticInfo());
  std::vector<float> left(512), right(512);
  float* out[2] = {left.data(), right.data()};
  wrapped->noteOn(60.0f, 1.0f);
  wrapped->process(nullptr, out, p, 512);  // warm up

  test::ScopedAllocationCounter counter;
  for (int block = 0; block < 100; ++block) {
    if (block == 50) wrapped->noteOn(67.0f, 1.0f);
    wrapped->process(nullptr, out, p, 512);
  }
  REQUIRE(counter.count() == 0);
}
```

Append to `tests/test_plaits_engine.cpp`:

```cpp
TEST_CASE("Macro Oscillator setNote changes pitch without a new note") {
  PlaitsEngine engine;
  engine.prepare(48000.0, 512);
  const auto p = defaultParams(PlaitsEngine::staticInfo());
  engine.noteOn(60.0f, 1.0f);
  render(engine, p, 24000, 512);
  engine.setNote(72.0f);
  const auto out = render(engine, p, 48000, 512);
  const double f = eg13::test::estimateFrequency(out, 48000.0, 12000, 48000);
  REQUIRE_THAT(f, Catch::Matchers::WithinRel(523.25, 0.01));
}
```

- [ ] **Step 5: Run to verify it fails**

Run: `cmake --build --preset mac --target eg13_tests`
Expected: compile errors: `'engine/native_rate_wrapper.h' file not found` and `no member named 'setNote'`.

- [ ] **Step 6: Add `setNote` to the engine interface**

In `core/engine/voice_engine.h`, add after `virtual void noteOff() = 0;`:

```cpp
  virtual void setNote(float note) = 0;  // pitch change without a new note (glide, bend)
```

In `core/engine/plaits_engine.h`, add after `void noteOff() override;`:

```cpp
  void setNote(float note) override { note_ = note; }
```

- [ ] **Step 7: Write `core/engine/native_rate_wrapper.h`**

```cpp
#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "engine/voice_engine.h"

namespace r8b {
class CDSPResampler24;
}

namespace eg13 {

// Runs an engine at its native sample rate and block size, and converts its
// output to the host rate with r8brain (linear phase). At the native rate it is
// a bit-exact pass-through. Notes and parameters reach the engine at native
// block boundaries, which is the hardware's control rate.
class NativeRateWrapper final : public VoiceEngine {
 public:
  // ~2.3 ms of latency at every common host rate (r8brain 7.6, measured).
  static constexpr double kTransitionBandPercent = 30.0;

  NativeRateWrapper(std::unique_ptr<VoiceEngine> inner, double nativeRate, int nativeBlock);
  ~NativeRateWrapper() override;

  void prepare(double hostRate, int maxBlock) override;
  void reset() override;
  void noteOn(float note, float velocity) override;
  void noteOff() override;
  void setNote(float note) override;
  // M1: `in` is ignored. Converting input to the native rate arrives with its
  // first consumer (M2).
  void process(const float* const* in, float* const* out, const ParamValues& p,
               int n) override;
  const EngineInfo& info() const override;

  int latencySamples() const { return latency_; }  // host-rate samples

 private:
  void renderNativeBlock(const ParamValues& p);

  std::unique_ptr<VoiceEngine> inner_;
  double nativeRate_;
  int nativeBlock_;
  bool bypass_ = true;
  int latency_ = 0;
  std::unique_ptr<r8b::CDSPResampler24> resampler_[2];
  std::vector<float> native_[2];
  std::vector<double> scratch_;
  std::vector<float> fifo_[2];
  size_t fifoRead_ = 0;
  size_t fifoSize_ = 0;
};

}  // namespace eg13
```

- [ ] **Step 8: Write `core/engine/native_rate_wrapper.cpp`**

```cpp
#include "engine/native_rate_wrapper.h"

#include <cmath>
#include <utility>

#include "CDSPResampler.h"

namespace eg13 {

NativeRateWrapper::NativeRateWrapper(std::unique_ptr<VoiceEngine> inner, double nativeRate,
                                     int nativeBlock)
    : inner_(std::move(inner)), nativeRate_(nativeRate), nativeBlock_(nativeBlock) {}

NativeRateWrapper::~NativeRateWrapper() = default;

void NativeRateWrapper::prepare(double hostRate, int) {
  inner_->prepare(nativeRate_, nativeBlock_);
  bypass_ = std::fabs(hostRate - nativeRate_) < 1e-6;
  latency_ = 0;
  scratch_.assign(size_t(nativeBlock_), 0.0);
  for (int ch = 0; ch < 2; ++ch) {
    resampler_[ch].reset();
    native_[ch].assign(size_t(nativeBlock_), 0.0f);
    fifo_[ch].clear();
  }
  if (!bypass_) {
    for (int ch = 0; ch < 2; ++ch) {
      resampler_[ch] = std::make_unique<r8b::CDSPResampler24>(nativeRate_, hostRate, nativeBlock_,
                                                              kTransitionBandPercent);
      fifo_[ch].assign(size_t(resampler_[ch]->getMaxOutLen(nativeBlock_)), 0.0f);
    }
    latency_ = int(std::lround(double(resampler_[0]->getInLenBeforeOutPos(0)) * hostRate /
                               nativeRate_));
  }
  reset();
}

void NativeRateWrapper::reset() {
  inner_->reset();
  for (auto& resampler : resampler_) {
    if (resampler) resampler->clear();
  }
  fifoRead_ = 0;
  fifoSize_ = 0;
}

void NativeRateWrapper::noteOn(float note, float velocity) { inner_->noteOn(note, velocity); }

void NativeRateWrapper::noteOff() { inner_->noteOff(); }

void NativeRateWrapper::setNote(float note) { inner_->setNote(note); }

const EngineInfo& NativeRateWrapper::info() const { return inner_->info(); }

void NativeRateWrapper::process(const float* const* in, float* const* out, const ParamValues& p,
                                int n) {
  if (bypass_) {
    inner_->process(in, out, p, n);
    return;
  }
  for (int i = 0; i < n; ++i) {
    // The converter withholds its first outputs, so a block can yield nothing.
    while (fifoSize_ == 0) renderNativeBlock(p);
    out[0][i] = fifo_[0][fifoRead_];
    out[1][i] = fifo_[1][fifoRead_];
    ++fifoRead_;
    --fifoSize_;
  }
}

void NativeRateWrapper::renderNativeBlock(const ParamValues& p) {
  float* native[2] = {native_[0].data(), native_[1].data()};
  inner_->process(nullptr, native, p, nativeBlock_);
  int produced = 0;
  for (int ch = 0; ch < 2; ++ch) {
    for (int j = 0; j < nativeBlock_; ++j) scratch_[size_t(j)] = double(native_[ch][size_t(j)]);
    double* converted = nullptr;
    // Both channels see identical block lengths, so they produce the same count.
    produced = resampler_[ch]->process(scratch_.data(), nativeBlock_, converted);
    for (int j = 0; j < produced; ++j) fifo_[ch][size_t(j)] = float(converted[j]);
  }
  fifoRead_ = 0;
  fifoSize_ = size_t(produced);
}

}  // namespace eg13
```

- [ ] **Step 9: Add the wrapper to `core/CMakeLists.txt`**

```cmake
add_library(eg13_core STATIC
  engine/plaits_engine.cpp
  engine/native_rate_wrapper.cpp
  synth/param_table.cpp
  synth/synth.cpp
  state/state.cpp)
target_include_directories(eg13_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(eg13_core PRIVATE eg13_eurorack eg13_nlohmann eg13_r8brain)
target_compile_options(eg13_core PRIVATE -Wall -Wextra -Werror)
```

`tests/CMakeLists.txt`: add `eg13_r8brain` to `target_link_libraries(eg13_tests ...)` (the wrapper test includes no r8brain header, but linking keeps the thread dependency explicit).

- [ ] **Step 10: Run the tests**

Run: `cmake --build --preset mac --target eg13_tests && ctest --preset mac`
Expected: all tests pass. If the latency test fails, report the measured `latency` and `delay` values; do not widen the tolerance.

- [ ] **Step 11: Commit**

```bash
git add .gitmodules third_party/r8brain-free-src cmake/r8brain.cmake CMakeLists.txt core tests
git commit -m "Add NativeRateWrapper: native-rate engines with r8brain conversion"
```

---
### Task 4: Voice allocator

**Files:**
- Create: `core/voice/voice_allocator.h`, `core/voice/voice_allocator.cpp`, `tests/test_voice_allocator.cpp`
- Modify: `core/CMakeLists.txt` (add `voice/voice_allocator.cpp`)

**Interfaces:**
- Consumes: `MonoNoteStack` (M0).
- Produces (all pure logic, no audio, no allocation):
  - `enum class VoiceMode { Poly = 0, Mono = 1, Legato = 2 };`
  - `struct VoiceAction { enum class Type { Trigger, Glide, Release }; Type type; int voice; int key; float velocity; };` — Trigger: start a note (retrigger); Glide: change pitch without retrigger; Release: gate off.
  - `struct VoiceActions { static constexpr int kMax = 8; VoiceAction items[kMax]; int count; void add(VoiceAction); };`
  - `class VoiceAllocator { static constexpr int kMaxVoices = 8; VoiceActions configure(VoiceMode, int voiceCount); VoiceActions noteOn(int key, float velocity); VoiceActions noteOff(int key); VoiceActions setSustain(bool down); VoiceActions allNotesOff(); void voiceIdle(int voice); VoiceMode mode() const; int voiceCount() const; bool sounding(int voice) const; int soundingCount() const; };`
  - Rules: Poly picks, in order, the voice already sounding this key; an idle voice; the released voice released longest ago; the held or sustained voice started longest ago. Mono and Legato use voice 0 with last-note priority (Mono retriggers, Legato glides while another key is held). With the sustain pedal down, note-offs mark voices sustained; pedal up releases them. `configure` with a new mode or count releases everything (voices keep sounding their tails until `voiceIdle`). Voice counts clamp to 1..8.

- [ ] **Step 1: Write the failing tests `tests/test_voice_allocator.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>

#include "voice/voice_allocator.h"

using namespace eg13;
using Type = VoiceAction::Type;

namespace {

VoiceAction only(const VoiceActions& actions) {
  REQUIRE(actions.count == 1);
  return actions.items[0];
}

}  // namespace

TEST_CASE("Poly gives each new key its own voice") {
  VoiceAllocator a;
  REQUIRE(only(a.noteOn(60, 1.0f)).voice == 0);
  REQUIRE(only(a.noteOn(64, 1.0f)).voice == 1);
  const VoiceAction third = only(a.noteOn(67, 0.5f));
  REQUIRE(third.type == Type::Trigger);
  REQUIRE(third.voice == 2);
  REQUIRE(third.key == 67);
  REQUIRE(third.velocity == 0.5f);
  REQUIRE(a.soundingCount() == 3);
}

TEST_CASE("Poly note-off releases the voice playing that key") {
  VoiceAllocator a;
  a.noteOn(60, 1.0f);
  a.noteOn(64, 1.0f);
  const VoiceAction off = only(a.noteOff(64));
  REQUIRE(off.type == Type::Release);
  REQUIRE(off.voice == 1);
  REQUIRE(a.noteOff(61).count == 0);
}

TEST_CASE("Repeating a sounding key retriggers the same voice") {
  VoiceAllocator a;
  a.noteOn(60, 1.0f);
  a.noteOff(60);
  const VoiceAction again = only(a.noteOn(60, 1.0f));
  REQUIRE(again.type == Type::Trigger);
  REQUIRE(again.voice == 0);
}

TEST_CASE("Idle voices are reused before stealing") {
  VoiceAllocator a;
  a.configure(VoiceMode::Poly, 2);
  a.noteOn(60, 1.0f);
  a.noteOn(62, 1.0f);
  a.noteOff(60);
  a.voiceIdle(0);
  REQUIRE(only(a.noteOn(64, 1.0f)).voice == 0);
}

TEST_CASE("Stealing prefers released voices over held ones") {
  VoiceAllocator a;
  a.configure(VoiceMode::Poly, 2);
  a.noteOn(60, 1.0f);  // voice 0, held
  a.noteOn(62, 1.0f);  // voice 1
  a.noteOff(62);       // voice 1 released, still sounding
  REQUIRE(only(a.noteOn(64, 1.0f)).voice == 1);
}

TEST_CASE("Stealing takes the oldest held voice when none are released") {
  VoiceAllocator a;
  a.configure(VoiceMode::Poly, 2);
  a.noteOn(60, 1.0f);
  a.noteOn(62, 1.0f);
  REQUIRE(only(a.noteOn(64, 1.0f)).voice == 0);
  REQUIRE(only(a.noteOn(65, 1.0f)).voice == 1);
}

TEST_CASE("Stealing prefers the voice released longest ago") {
  VoiceAllocator a;
  a.configure(VoiceMode::Poly, 3);
  a.noteOn(60, 1.0f);
  a.noteOn(62, 1.0f);
  a.noteOn(64, 1.0f);
  a.noteOff(64);
  a.noteOff(60);
  REQUIRE(only(a.noteOn(65, 1.0f)).voice == 2);
}

TEST_CASE("Sustain pedal holds released notes until pedal up") {
  VoiceAllocator a;
  a.noteOn(60, 1.0f);
  a.noteOn(64, 1.0f);
  REQUIRE(a.setSustain(true).count == 0);
  REQUIRE(a.noteOff(60).count == 0);
  REQUIRE(a.noteOff(64).count == 0);
  a.noteOn(67, 1.0f);  // still held when the pedal comes up
  const VoiceActions up = a.setSustain(false);
  REQUIRE(up.count == 2);
  REQUIRE(up.items[0].type == Type::Release);
  REQUIRE(up.items[0].voice == 0);
  REQUIRE(up.items[1].voice == 1);
}

TEST_CASE("Stealing treats sustained voices like held ones") {
  VoiceAllocator a;
  a.configure(VoiceMode::Poly, 2);
  a.setSustain(true);
  a.noteOn(60, 1.0f);
  a.noteOff(60);       // voice 0 sustained
  a.noteOn(62, 1.0f);  // voice 1 held
  REQUIRE(only(a.noteOn(64, 1.0f)).voice == 0);  // oldest start, both held/sustained
}

TEST_CASE("Mono retriggers each new note and returns to the previous key") {
  VoiceAllocator a;
  a.configure(VoiceMode::Mono, 8);
  REQUIRE(only(a.noteOn(60, 1.0f)).type == Type::Trigger);
  const VoiceAction second = only(a.noteOn(67, 0.7f));
  REQUIRE(second.type == Type::Trigger);
  REQUIRE(second.voice == 0);
  const VoiceAction back = only(a.noteOff(67));
  REQUIRE(back.type == Type::Trigger);
  REQUIRE(back.key == 60);
  const VoiceAction off = only(a.noteOff(60));
  REQUIRE(off.type == Type::Release);
  REQUIRE(off.voice == 0);
}

TEST_CASE("Legato glides while another key is held") {
  VoiceAllocator a;
  a.configure(VoiceMode::Legato, 8);
  REQUIRE(only(a.noteOn(60, 1.0f)).type == Type::Trigger);
  const VoiceAction glide = only(a.noteOn(67, 1.0f));
  REQUIRE(glide.type == Type::Glide);
  REQUIRE(glide.key == 67);
  const VoiceAction back = only(a.noteOff(67));
  REQUIRE(back.type == Type::Glide);
  REQUIRE(back.key == 60);
  REQUIRE(only(a.noteOff(60)).type == Type::Release);
  REQUIRE(only(a.noteOn(62, 1.0f)).type == Type::Trigger);
}

TEST_CASE("Mono with the sustain pedal releases on pedal up") {
  VoiceAllocator a;
  a.configure(VoiceMode::Mono, 1);
  a.setSustain(true);
  a.noteOn(60, 1.0f);
  REQUIRE(a.noteOff(60).count == 0);
  const VoiceAction up = only(a.setSustain(false));
  REQUIRE(up.type == Type::Release);
  REQUIRE(up.voice == 0);
}

TEST_CASE("configure releases sounding notes when the mode or count changes") {
  VoiceAllocator a;
  a.noteOn(60, 1.0f);
  a.noteOn(64, 1.0f);
  const VoiceActions changed = a.configure(VoiceMode::Poly, 4);
  REQUIRE(changed.count == 2);
  REQUIRE(changed.items[0].type == Type::Release);
  REQUIRE(a.configure(VoiceMode::Poly, 4).count == 0);  // no change, no actions
  REQUIRE(a.configure(VoiceMode::Legato, 4).count == 0);  // nothing held any more
  REQUIRE(a.mode() == VoiceMode::Legato);
}

TEST_CASE("Voice counts clamp to 1..8") {
  VoiceAllocator a;
  a.configure(VoiceMode::Poly, 0);
  REQUIRE(a.voiceCount() == 1);
  a.configure(VoiceMode::Poly, 99);
  REQUIRE(a.voiceCount() == 8);
}

TEST_CASE("allNotesOff releases held and sustained voices and lifts the pedal") {
  VoiceAllocator a;
  a.noteOn(60, 1.0f);
  a.setSustain(true);
  a.noteOn(64, 1.0f);
  a.noteOff(64);
  REQUIRE(a.allNotesOff().count == 2);
  a.noteOn(67, 1.0f);
  REQUIRE(a.noteOff(67).count == 1);  // pedal is up again
}

TEST_CASE("voiceIdle frees only released voices") {
  VoiceAllocator a;
  a.noteOn(60, 1.0f);
  a.voiceIdle(0);  // still held: ignored
  REQUIRE(a.sounding(0));
  a.noteOff(60);
  a.voiceIdle(0);
  REQUIRE_FALSE(a.sounding(0));
  REQUIRE(a.soundingCount() == 0);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build --preset mac --target eg13_tests`
Expected: compile error, `'voice/voice_allocator.h' file not found`.

- [ ] **Step 3: Write `core/voice/voice_allocator.h`**

```cpp
#pragma once

#include "voice/mono_note_stack.h"

namespace eg13 {

enum class VoiceMode { Poly = 0, Mono = 1, Legato = 2 };

struct VoiceAction {
  enum class Type { Trigger, Glide, Release };
  Type type;
  int voice;
  int key;
  float velocity;
};

struct VoiceActions {
  static constexpr int kMax = 8;
  VoiceAction items[kMax] = {};
  int count = 0;
  void add(VoiceAction action) {
    if (count < kMax) items[count++] = action;
  }
};

// Decides which voice plays each note. Pure logic: no audio, no allocation.
class VoiceAllocator {
 public:
  static constexpr int kMaxVoices = 8;

  VoiceActions configure(VoiceMode mode, int voiceCount);
  VoiceActions noteOn(int key, float velocity);
  VoiceActions noteOff(int key);
  VoiceActions setSustain(bool down);
  VoiceActions allNotesOff();
  void voiceIdle(int voice);  // the voice went silent after its release

  VoiceMode mode() const { return mode_; }
  int voiceCount() const { return voiceCount_; }
  bool sounding(int voice) const { return slots_[voice].sounding; }
  int soundingCount() const;

 private:
  struct Slot {
    int key = -1;
    bool held = false;
    bool sustained = false;
    bool sounding = false;
    unsigned startOrder = 0;
    unsigned releaseOrder = 0;
  };

  int pickPolyVoice(int key) const;
  VoiceAction release(int voice);

  VoiceMode mode_ = VoiceMode::Poly;
  int voiceCount_ = kMaxVoices;
  bool sustain_ = false;
  unsigned clock_ = 0;
  Slot slots_[kMaxVoices];
  MonoNoteStack stack_;
};

}  // namespace eg13
```

- [ ] **Step 4: Write `core/voice/voice_allocator.cpp`**

```cpp
#include "voice/voice_allocator.h"

namespace eg13 {

VoiceActions VoiceAllocator::configure(VoiceMode mode, int voiceCount) {
  if (voiceCount < 1) voiceCount = 1;
  if (voiceCount > kMaxVoices) voiceCount = kMaxVoices;
  if (mode == mode_ && voiceCount == voiceCount_) return {};
  VoiceActions actions = allNotesOff();
  mode_ = mode;
  voiceCount_ = voiceCount;
  return actions;
}

VoiceActions VoiceAllocator::noteOn(int key, float velocity) {
  VoiceActions actions;
  if (mode_ != VoiceMode::Poly) {
    const bool wasEmpty = stack_.empty();
    stack_.noteOn(key, velocity);
    Slot& s = slots_[0];
    s.key = key;
    s.held = true;
    s.sustained = false;
    s.sounding = true;
    if (mode_ == VoiceMode::Legato && !wasEmpty) {
      actions.add({VoiceAction::Type::Glide, 0, key, velocity});
    } else {
      s.startOrder = ++clock_;
      actions.add({VoiceAction::Type::Trigger, 0, key, velocity});
    }
    return actions;
  }
  const int v = pickPolyVoice(key);
  Slot& s = slots_[v];
  s.key = key;
  s.held = true;
  s.sustained = false;
  s.sounding = true;
  s.startOrder = ++clock_;
  actions.add({VoiceAction::Type::Trigger, v, key, velocity});
  return actions;
}

VoiceActions VoiceAllocator::noteOff(int key) {
  VoiceActions actions;
  if (mode_ != VoiceMode::Poly) {
    const bool wasTop = !stack_.empty() && stack_.topKey() == key;
    stack_.noteOff(key);
    Slot& s = slots_[0];
    if (stack_.empty()) {
      if (!s.held) return actions;
      s.held = false;
      if (sustain_) {
        s.sustained = true;
      } else {
        actions.add(release(0));
      }
    } else if (wasTop) {
      s.key = stack_.topKey();
      if (mode_ == VoiceMode::Legato) {
        actions.add({VoiceAction::Type::Glide, 0, s.key, stack_.topVelocity()});
      } else {
        s.startOrder = ++clock_;
        actions.add({VoiceAction::Type::Trigger, 0, s.key, stack_.topVelocity()});
      }
    }
    return actions;
  }
  for (int v = 0; v < kMaxVoices; ++v) {
    Slot& s = slots_[v];
    if (s.held && s.key == key) {
      s.held = false;
      if (sustain_) {
        s.sustained = true;
      } else {
        actions.add(release(v));
      }
    }
  }
  return actions;
}

VoiceActions VoiceAllocator::setSustain(bool down) {
  VoiceActions actions;
  sustain_ = down;
  if (!down) {
    for (int v = 0; v < kMaxVoices; ++v) {
      if (slots_[v].sustained) actions.add(release(v));
    }
  }
  return actions;
}

VoiceActions VoiceAllocator::allNotesOff() {
  VoiceActions actions;
  stack_.clear();
  sustain_ = false;
  for (int v = 0; v < kMaxVoices; ++v) {
    if (slots_[v].held || slots_[v].sustained) actions.add(release(v));
  }
  return actions;
}

void VoiceAllocator::voiceIdle(int voice) {
  Slot& s = slots_[voice];
  if (s.held || s.sustained) return;
  s.sounding = false;
  s.key = -1;
}

int VoiceAllocator::soundingCount() const {
  int count = 0;
  for (const Slot& s : slots_) count += s.sounding ? 1 : 0;
  return count;
}

int VoiceAllocator::pickPolyVoice(int key) const {
  for (int v = 0; v < voiceCount_; ++v) {
    if (slots_[v].sounding && slots_[v].key == key) return v;
  }
  for (int v = 0; v < voiceCount_; ++v) {
    if (!slots_[v].sounding) return v;
  }
  int best = -1;
  for (int v = 0; v < voiceCount_; ++v) {
    const Slot& s = slots_[v];
    if (s.held || s.sustained) continue;
    if (best < 0 || s.releaseOrder < slots_[best].releaseOrder) best = v;
  }
  if (best >= 0) return best;
  best = 0;
  for (int v = 1; v < voiceCount_; ++v) {
    if (slots_[v].startOrder < slots_[best].startOrder) best = v;
  }
  return best;
}

VoiceAction VoiceAllocator::release(int voice) {
  Slot& s = slots_[voice];
  s.held = false;
  s.sustained = false;
  s.releaseOrder = ++clock_;
  return {VoiceAction::Type::Release, voice, s.key, 0.0f};
}

}  // namespace eg13
```

- [ ] **Step 5: Add `voice/voice_allocator.cpp` to `core/CMakeLists.txt` and run the tests**

Run: `cmake --build --preset mac --target eg13_tests && ctest --preset mac`
Expected: all tests pass.

- [ ] **Step 6: Commit**

```bash
git add core/voice/voice_allocator.h core/voice/voice_allocator.cpp core/CMakeLists.txt tests/test_voice_allocator.cpp
git commit -m "Add voice allocator: poly/mono/legato, stealing and sustain"
```

---

### Task 5: Voice

**Files:**
- Create: `core/voice/voice.h`, `core/voice/voice.cpp`, `core/voice/source_factory.h`, `core/voice/source_factory.cpp`, `tests/test_voice.cpp`
- Modify: `core/CMakeLists.txt` (add `voice/voice.cpp`, `voice/source_factory.cpp`)

**Interfaces:**
- Consumes: `NativeRateWrapper` (Task 3), `VoiceEngine::setNote` (Task 3), `PlaitsEngine`, `test::SineEngine`, `test::ImpulseEngine`, `ScopedAllocationCounter`.
- Produces:
  - `struct SourceSpec { std::unique_ptr<VoiceEngine> engine; double nativeRate; int nativeBlock; }; using SourceFactory = SourceSpec (*)(); SourceSpec makePlaitsSource();` (Plaits: 48000 Hz, 12-sample blocks).
  - `class Voice { static constexpr int kControlBlock = 16; explicit Voice(SourceSpec source); void prepare(double hostRate, int maxBlock); void reset(); void trigger(int key, float velocity); void glideTo(int key); void release(); void setPitchOffset(float semitones); void setGlideSeconds(float seconds); void process(float* const* out, const ParamValues& p, int n); bool active() const; bool gate() const; float currentNote() const; int latencySamples() const; };`
  - `process` ADDS into `out[0]`, `out[1]`. Pitch (glide target, offset) updates every 16 host samples. Glide only applies when the voice was already sounding. Triggering an inactive voice resets its source first so no stale converter samples leak into the new note. The voice becomes inactive once its gate is off and its output has stayed below −90 dBFS (3.1623e-5) for 50 ms. If the engine outputs NaN or Inf, the voice resets the engine, adds nothing for that block and becomes inactive (spec §9).

- [ ] **Step 1: Write the failing tests `tests/test_voice.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <limits>
#include <memory>
#include <vector>

#include "alloc_counter.h"
#include "engine/plaits_engine.h"
#include "test_engines.h"
#include "test_helpers.h"
#include "voice/source_factory.h"
#include "voice/voice.h"

using namespace eg13;

namespace {

SourceSpec sine() { return {std::make_unique<test::SineEngine>(), 48000.0, 12}; }

std::vector<float> run(Voice& voice, int n, const ParamValues& p = ParamValues{}) {
  std::vector<float> left(static_cast<size_t>(n), 0.0f), right(static_cast<size_t>(n), 0.0f);
  for (int done = 0; done < n;) {
    const int count = std::min(256, n - done);
    float* out[2] = {left.data() + done, right.data() + done};
    voice.process(out, p, count);
    done += count;
  }
  return left;
}

}  // namespace

TEST_CASE("An idle voice adds nothing") {
  Voice voice(sine());
  voice.prepare(44100.0, 256);
  REQUIRE_FALSE(voice.active());
  for (float s : run(voice, 4410)) REQUIRE(s == 0.0f);
}

TEST_CASE("A triggered voice plays its key in tune") {
  Voice voice(sine());
  voice.prepare(44100.0, 256);
  voice.trigger(69, 1.0f);
  const auto out = run(voice, 44100);
  REQUIRE_THAT(test::estimateFrequency(out, 44100.0, 4410, 44100),
               Catch::Matchers::WithinRel(440.0, 0.001));
}

TEST_CASE("The pitch offset transposes the voice") {
  Voice voice(sine());
  voice.prepare(48000.0, 256);
  voice.setPitchOffset(12.0f);
  voice.trigger(57, 1.0f);
  const auto out = run(voice, 48000);
  REQUIRE_THAT(test::estimateFrequency(out, 48000.0, 4800, 48000),
               Catch::Matchers::WithinRel(440.0, 0.001));
}

TEST_CASE("Glide moves gradually to a new key while sounding") {
  Voice voice(sine());
  voice.prepare(48000.0, 256);
  voice.setGlideSeconds(0.1f);
  voice.trigger(60, 1.0f);
  REQUIRE(voice.currentNote() == 60.0f);
  run(voice, 4800);
  voice.glideTo(72);
  run(voice, 480);  // 10 ms
  REQUIRE(voice.currentNote() > 60.0f);
  REQUIRE(voice.currentNote() < 72.0f);
  run(voice, 96000);
  REQUIRE(voice.currentNote() == 72.0f);
}

TEST_CASE("A voice that was idle starts at its key even with glide on") {
  Voice voice(sine());
  voice.prepare(48000.0, 256);
  voice.setGlideSeconds(1.0f);
  voice.trigger(72, 1.0f);
  REQUIRE(voice.currentNote() == 72.0f);
}

TEST_CASE("A released voice goes idle 50 ms after it falls silent") {
  Voice voice(sine());
  voice.prepare(48000.0, 256);
  voice.trigger(60, 1.0f);
  run(voice, 4800);
  voice.release();
  REQUIRE_FALSE(voice.gate());
  run(voice, 1920);  // 40 ms of silence: less than the 50 ms limit
  REQUIRE(voice.active());
  run(voice, 4800);
  REQUIRE_FALSE(voice.active());
}

TEST_CASE("A voice whose engine outputs NaN is muted and freed") {
  struct NanEngine final : VoiceEngine {
    void prepare(double, int) override {}
    void reset() override {}
    void noteOn(float, float) override {}
    void noteOff() override {}
    void setNote(float) override {}
    void process(const float* const*, float* const* out, const ParamValues&, int n) override {
      for (int i = 0; i < n; ++i) out[0][i] = out[1][i] = std::numeric_limits<float>::quiet_NaN();
    }
    const EngineInfo& info() const override {
      static const EngineInfo kInfo = {"nan", "NaN", 0, nullptr};
      return kInfo;
    }
  };
  Voice voice({std::make_unique<NanEngine>(), 48000.0, 12});
  voice.prepare(48000.0, 256);
  voice.trigger(60, 1.0f);
  for (float s : run(voice, 512)) REQUIRE(s == 0.0f);
  REQUIRE_FALSE(voice.active());
}

TEST_CASE("The default Plaits source makes sound") {
  Voice voice(makePlaitsSource());
  voice.prepare(44100.0, 256);
  voice.trigger(60, 1.0f);
  REQUIRE(test::rms(run(voice, 22050), 4410, 22050) > 0.01);
}

TEST_CASE("Voice processing does not allocate") {
  Voice voice(makePlaitsSource());
  voice.prepare(44100.0, 256);
  std::vector<float> left(256), right(256);
  float* out[2] = {left.data(), right.data()};
  const ParamValues p;
  voice.trigger(60, 1.0f);
  voice.process(out, p, 256);
  test::ScopedAllocationCounter counter;
  for (int block = 0; block < 200; ++block) {
    if (block == 20) voice.glideTo(67);
    if (block == 100) voice.release();
    if (block == 150) voice.trigger(64, 0.5f);
    voice.process(out, p, 256);
  }
  REQUIRE(counter.count() == 0);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build --preset mac --target eg13_tests`
Expected: compile error, `'voice/source_factory.h' file not found`.

- [ ] **Step 3: Write `core/voice/source_factory.h` and `.cpp`**

```cpp
// source_factory.h
#pragma once

#include <memory>

#include "engine/voice_engine.h"

namespace eg13 {

// A source engine plus the native rate and block size it runs at.
struct SourceSpec {
  std::unique_ptr<VoiceEngine> engine;
  double nativeRate;
  int nativeBlock;
};

using SourceFactory = SourceSpec (*)();

SourceSpec makePlaitsSource();

}  // namespace eg13
```

```cpp
// source_factory.cpp
#include "voice/source_factory.h"

#include "engine/plaits_engine.h"

namespace eg13 {

SourceSpec makePlaitsSource() {
  return {std::make_unique<PlaitsEngine>(), PlaitsEngine::kNativeRate, 12};
}

}  // namespace eg13
```

- [ ] **Step 4: Write `core/voice/voice.h`**

```cpp
#pragma once

#include "engine/native_rate_wrapper.h"
#include "voice/source_factory.h"

namespace eg13 {

// One synth voice: the source engine at its native rate, plus note, glide and
// bend handling and silence detection.
class Voice {
 public:
  static constexpr int kControlBlock = 16;  // pitch updates every 16 host samples

  explicit Voice(SourceSpec source);

  void prepare(double hostRate, int maxBlock);  // main thread
  void reset();
  void trigger(int key, float velocity);
  void glideTo(int key);
  void release();
  void setPitchOffset(float semitones) { pitchOffset_ = semitones; }
  void setGlideSeconds(float seconds) { glideSeconds_ = seconds; }

  // Adds n samples into out[0], out[1].
  void process(float* const* out, const ParamValues& p, int n);

  bool active() const { return active_; }
  bool gate() const { return gate_; }
  float currentNote() const { return currentNote_; }
  int latencySamples() const { return source_.latencySamples(); }

 private:
  bool blockIsFinite(int count) const;

  NativeRateWrapper source_;
  double hostRate_ = 48000.0;
  float targetNote_ = 60.0f;
  float currentNote_ = 60.0f;
  float pitchOffset_ = 0.0f;
  float glideSeconds_ = 0.0f;
  bool gate_ = false;
  bool active_ = false;
  int silentSamples_ = 0;
  int silenceLimit_ = 2400;
  float buffer_[2][kControlBlock] = {};
};

}  // namespace eg13
```

- [ ] **Step 5: Write `core/voice/voice.cpp`**

```cpp
#include "voice/voice.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace eg13 {

namespace {
constexpr float kSilenceThreshold = 3.1623e-5f;  // -90 dBFS
constexpr double kSilenceSeconds = 0.05;
}  // namespace

Voice::Voice(SourceSpec source)
    : source_(std::move(source.engine), source.nativeRate, source.nativeBlock) {}

void Voice::prepare(double hostRate, int maxBlock) {
  hostRate_ = hostRate;
  silenceLimit_ = int(hostRate * kSilenceSeconds);
  source_.prepare(hostRate, maxBlock);
  reset();
}

void Voice::reset() {
  source_.reset();
  gate_ = false;
  active_ = false;
  silentSamples_ = 0;
}

void Voice::trigger(int key, float velocity) {
  targetNote_ = float(key);
  if (!active_) {
    source_.reset();  // drop stale converter samples from the previous note
    currentNote_ = targetNote_;
  } else if (glideSeconds_ <= 0.0f) {
    currentNote_ = targetNote_;
  }
  gate_ = true;
  active_ = true;
  silentSamples_ = 0;
  source_.noteOn(currentNote_ + pitchOffset_, velocity);
}

void Voice::glideTo(int key) {
  targetNote_ = float(key);
  if (glideSeconds_ <= 0.0f) currentNote_ = targetNote_;
}

bool Voice::blockIsFinite(int count) const {
  for (int i = 0; i < count; ++i) {
    if (!std::isfinite(buffer_[0][i]) || !std::isfinite(buffer_[1][i])) return false;
  }
  return true;
}

void Voice::release() {
  gate_ = false;
  source_.noteOff();
}

void Voice::process(float* const* out, const ParamValues& p, int n) {
  if (!active_) return;
  const float coeff =
      glideSeconds_ > 0.0f
          ? float(std::exp(-double(kControlBlock) / (double(glideSeconds_) * hostRate_)))
          : 0.0f;
  for (int done = 0; done < n;) {
    const int count = std::min(kControlBlock, n - done);
    currentNote_ = targetNote_ + (currentNote_ - targetNote_) * coeff;
    if (std::fabs(currentNote_ - targetNote_) < 1e-4f) currentNote_ = targetNote_;
    source_.setNote(currentNote_ + pitchOffset_);

    float* block[2] = {buffer_[0], buffer_[1]};
    source_.process(nullptr, block, p, count);
    if (!blockIsFinite(count)) {
      // Spec §9: a NaN/Inf from an engine resets it and mutes the voice.
      source_.reset();
      gate_ = false;
      active_ = false;
      return;
    }
    float peak = 0.0f;
    for (int i = 0; i < count; ++i) {
      out[0][done + i] += buffer_[0][i];
      out[1][done + i] += buffer_[1][i];
      peak = std::max(peak, std::max(std::fabs(buffer_[0][i]), std::fabs(buffer_[1][i])));
    }
    done += count;

    if (!gate_ && peak < kSilenceThreshold) {
      silentSamples_ += count;
      if (silentSamples_ >= silenceLimit_) {
        active_ = false;
        return;
      }
    } else {
      silentSamples_ = 0;
    }
  }
}

}  // namespace eg13
```

- [ ] **Step 6: Add the sources to `core/CMakeLists.txt` and run the tests**

Add `voice/voice_allocator.cpp` (if not already), `voice/voice.cpp` and `voice/source_factory.cpp` to `add_library(eg13_core ...)`.

Run: `cmake --build --preset mac --target eg13_tests && ctest --preset mac`
Expected: all tests pass.

- [ ] **Step 7: Commit**

```bash
git add core/voice core/CMakeLists.txt tests/test_voice.cpp
git commit -m "Add Voice: native-rate source, glide, pitch offset and silence detection"
```

---
### Task 6: Parameter table v2

**Files:**
- Create: `core/synth/plugin_kind.h`, `tests/test_param_table.cpp`
- Modify: `core/synth/param_table.h`, `core/synth/param_table.cpp`, `core/synth/synth.h`, `core/synth/synth.cpp`, `tests/test_synth.cpp`, `plugin/src/eg13_plugin.cpp`

**Interfaces:**
- Consumes: `params::id`, `params::Slot`, `params::kMasterPart`, `params::kMasterLevel` (M0); `PlaitsEngine::staticInfo()`.
- Produces (`core/synth/param_table.h`):
  - `enum class PluginKind { Instrument, Effect };` (in `synth/plugin_kind.h`, included by `param_table.h` and `synth.h`).
  - Constants: `kNumParts = 2`, `kSourceParamCount = 8`, `kPartParamCount = 14`, `kMasterParamCount = 4`, `kParamsPerPart = 22`, `kParamCount = 48`.
  - `enum class PartParam : uint32_t { Level = 1, Pan, Glide, Transpose, BaseNote, MidiChannel, KeyLow, KeyHigh, VoiceMode, VoiceCount, Trigger, BendRange, Mono, Output };` — the enum value is the parameter-ID index within `Slot::Part`.
  - `enum class MasterParam : uint32_t { Level = 0, Dry, Mode, SplitNote };` — the ID index for part `0xFF`, `Slot::Part`.
  - `enum class PartMode { Single = 0, Layer = 1, Split = 2, Multi = 3 };`
  - `constexpr size_t sourceParamIndex(int part, int p)` (p = 0..7), `constexpr size_t partParamIndex(int part, PartParam)`, `constexpr size_t masterParamIndex(MasterParam)` — positions in the table.
  - `enum class ParamFormat { Engine, Percent, Pan, GlideTime, Semitones, BendRange, Note, Channel, VoiceMode, VoiceCount, Trigger, OnOff, OutputBus, PartMode };`
  - `struct ParamSpec { uint32_t id; const char* name; const char* module; float minValue; float maxValue; float defaultValue; float effectDefault; bool stepped; ParamFormat format; };`
  - `const ParamSpec& paramSpec(size_t index); int paramIndex(uint32_t id); float paramDefault(size_t index, PluginKind kind); float clampParam(size_t index, double value); void formatParam(size_t index, double value, char* buf, size_t size); float glideSeconds(float normalized);`
  - Values are stored in natural units: P1–P8 0..1, level 0..1, pan −1..1, glide 0..1 (2·v² seconds), transpose −24..24, notes 0..127 (60 = "C4"), MIDI channel 0..16 (0 = Omni), voice mode 0..2, voice count 1..8, trigger 0..1 (1 = Always on), bend range 0..24, Mono 0..1, output 0..1 (1 = Bus 2), mode 0..3, split note 0..127, Dry 0..1 (default 0 for the instrument, 1 for the FX). `clampParam` clamps, rounds stepped values, and maps NaN to the minimum.

Table layout (index order): for part 0 then part 1: Source P1–P8, then the 14 part settings in `PartParam` order; then the 4 master parameters.

| PartParam | Name | Range | Default | Stepped | Format |
|---|---|---|---|---|---|
| Level | Level | 0..1 | 1 | no | Percent |
| Pan | Pan | −1..1 | 0 | no | Pan |
| Glide | Glide | 0..1 | 0 | no | GlideTime |
| Transpose | Transpose | −24..24 | 0 | yes | Semitones |
| BaseNote | Base Note | 0..127 | 60 | yes | Note |
| MidiChannel | MIDI Channel | 0..16 | 0 | yes | Channel |
| KeyLow | Key Low | 0..127 | 0 | yes | Note |
| KeyHigh | Key High | 0..127 | 127 | yes | Note |
| VoiceMode | Voice Mode | 0..2 | 0 | yes | VoiceMode |
| VoiceCount | Voices | 1..8 | 8 | yes | VoiceCount |
| Trigger | Trigger | 0..1 | 0 | yes | Trigger |
| BendRange | Bend Range | 0..24 | 2 | yes | BendRange |
| Mono | Mono | 0..1 | 0 | yes | OnOff |
| Output | Output | 0..1 | 0 | yes | OutputBus |

| MasterParam | Name | Range | Default (instrument / FX) | Stepped | Format |
|---|---|---|---|---|---|
| Level | Level | 0..1 | 0.8 / 0.8 | no | Percent |
| Dry | Dry | 0..1 | 0 / 1 | no | Percent |
| Mode | Mode | 0..3 | 0 / 0 | yes | PartMode |
| SplitNote | Split Note | 0..127 | 60 / 60 | yes | Note |

Modules: `"Part A/Source"`, `"Part B/Source"`, `"Part A"`, `"Part B"`, `"Master"`.

- [ ] **Step 1: Write the failing tests `tests/test_param_table.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <set>
#include <string>

#include "params/param_ids.h"
#include "synth/param_table.h"

using namespace eg13;

namespace {

std::string text(size_t index, double value) {
  char buf[64];
  formatParam(index, value, buf, sizeof(buf));
  return buf;
}

}  // namespace

TEST_CASE("The table has 48 parameters with unique IDs") {
  REQUIRE(kParamCount == 48);
  std::set<uint32_t> ids;
  for (size_t i = 0; i < kParamCount; ++i) ids.insert(paramSpec(i).id);
  REQUIRE(ids.size() == kParamCount);
  for (size_t i = 0; i < kParamCount; ++i) REQUIRE(paramIndex(paramSpec(i).id) == int(i));
  REQUIRE(paramIndex(0xDEADBEEF) == -1);
}

TEST_CASE("M0 parameter IDs keep their meaning and range") {
  const int model = paramIndex(params::id(0, params::Slot::Source, 1));
  REQUIRE(model == int(sourceParamIndex(0, 0)));
  REQUIRE(std::string(paramSpec(size_t(model)).name) == "Model");
  REQUIRE(paramSpec(size_t(model)).minValue == 0.0f);
  REQUIRE(paramSpec(size_t(model)).maxValue == 1.0f);
  REQUIRE(paramIndex(params::id(0, params::Slot::Source, 8)) == int(sourceParamIndex(0, 7)));
  const size_t level = masterParamIndex(MasterParam::Level);
  REQUIRE(paramSpec(level).id == params::kMasterLevel);
  REQUIRE(paramSpec(level).defaultValue == 0.8f);
}

TEST_CASE("Index helpers agree with parameter IDs") {
  REQUIRE(paramSpec(sourceParamIndex(1, 2)).id == params::id(1, params::Slot::Source, 3));
  REQUIRE(paramSpec(partParamIndex(0, PartParam::Level)).id == params::id(0, params::Slot::Part, 1));
  REQUIRE(paramSpec(partParamIndex(1, PartParam::Output)).id ==
          params::id(1, params::Slot::Part, 14));
  REQUIRE(paramSpec(masterParamIndex(MasterParam::SplitNote)).id ==
          params::id(params::kMasterPart, params::Slot::Part, 3));
  REQUIRE(std::string(paramSpec(partParamIndex(1, PartParam::MidiChannel)).name) == "MIDI Channel");
  REQUIRE(std::string(paramSpec(partParamIndex(1, PartParam::MidiChannel)).module) == "Part B");
  REQUIRE(std::string(paramSpec(sourceParamIndex(1, 0)).module) == "Part B/Source");
}

TEST_CASE("Dry defaults differ between the instrument and the FX") {
  const size_t dry = masterParamIndex(MasterParam::Dry);
  REQUIRE(paramDefault(dry, PluginKind::Instrument) == 0.0f);
  REQUIRE(paramDefault(dry, PluginKind::Effect) == 1.0f);
  const size_t voices = partParamIndex(0, PartParam::VoiceCount);
  REQUIRE(paramDefault(voices, PluginKind::Instrument) == 8.0f);
  REQUIRE(paramDefault(voices, PluginKind::Effect) == 8.0f);
}

TEST_CASE("clampParam clamps, rounds stepped values and maps NaN to the minimum") {
  const size_t voices = partParamIndex(0, PartParam::VoiceCount);
  REQUIRE(clampParam(voices, 3.6) == 4.0f);
  REQUIRE(clampParam(voices, 0.0) == 1.0f);
  REQUIRE(clampParam(voices, 99.0) == 8.0f);
  REQUIRE(clampParam(voices, std::numeric_limits<double>::quiet_NaN()) == 1.0f);
  const size_t pan = partParamIndex(0, PartParam::Pan);
  REQUIRE(clampParam(pan, -0.25) == -0.25f);
  REQUIRE(clampParam(pan, -3.0) == -1.0f);
  REQUIRE(clampParam(sourceParamIndex(0, 1), 1.5) == 1.0f);
}

TEST_CASE("Parameters format readable values") {
  REQUIRE(text(partParamIndex(0, PartParam::BaseNote), 60) == "C4");
  REQUIRE(text(partParamIndex(0, PartParam::BaseNote), 61) == "C#4");
  REQUIRE(text(partParamIndex(0, PartParam::KeyLow), 0) == "C-1");
  REQUIRE(text(partParamIndex(0, PartParam::MidiChannel), 0) == "Omni");
  REQUIRE(text(partParamIndex(0, PartParam::MidiChannel), 10) == "10");
  REQUIRE(text(partParamIndex(0, PartParam::Pan), -0.5) == "L50");
  REQUIRE(text(partParamIndex(0, PartParam::Pan), 0.0) == "C");
  REQUIRE(text(partParamIndex(0, PartParam::Pan), 1.0) == "R100");
  REQUIRE(text(partParamIndex(0, PartParam::Glide), 0.0) == "Off");
  REQUIRE(text(partParamIndex(0, PartParam::Glide), 0.5) == "500 ms");
  REQUIRE(text(partParamIndex(0, PartParam::Glide), 1.0) == "2.00 s");
  REQUIRE(text(partParamIndex(0, PartParam::Transpose), 3) == "+3 st");
  REQUIRE(text(partParamIndex(0, PartParam::BendRange), 2) == "+/-2 st");
  REQUIRE(text(partParamIndex(0, PartParam::VoiceMode), 2) == "Legato");
  REQUIRE(text(partParamIndex(0, PartParam::VoiceCount), 4) == "4");
  REQUIRE(text(partParamIndex(0, PartParam::Trigger), 1) == "Always on");
  REQUIRE(text(partParamIndex(0, PartParam::Mono), 1) == "On");
  REQUIRE(text(partParamIndex(0, PartParam::Output), 1) == "Bus 2");
  REQUIRE(text(masterParamIndex(MasterParam::Mode), 2) == "Split");
  REQUIRE(text(masterParamIndex(MasterParam::Dry), 0.25) == "25%");
  REQUIRE(text(sourceParamIndex(1, 0), (8.0 + 0.5) / 24.0) == "Virtual analog");
}

TEST_CASE("glideSeconds maps 0..1 to 0..2 s quadratically") {
  REQUIRE(glideSeconds(0.0f) == 0.0f);
  REQUIRE(glideSeconds(0.5f) == 0.5f);
  REQUIRE(glideSeconds(1.0f) == 2.0f);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build --preset mac --target eg13_tests`
Expected: compile errors (`sourceParamIndex`, `PartParam`, `paramDefault`... not declared).

- [ ] **Step 3: Write `core/synth/plugin_kind.h`**

```cpp
#pragma once

namespace eg13 {

enum class PluginKind { Instrument, Effect };

}  // namespace eg13
```

- [ ] **Step 4: Replace `core/synth/param_table.h`**

```cpp
#pragma once

#include <cstddef>
#include <cstdint>

#include "synth/plugin_kind.h"

namespace eg13 {

constexpr int kNumParts = 2;
constexpr size_t kSourceParamCount = 8;

// Value = parameter-ID index within Slot::Part. Never renumber.
enum class PartParam : uint32_t {
  Level = 1,
  Pan,
  Glide,
  Transpose,
  BaseNote,
  MidiChannel,
  KeyLow,
  KeyHigh,
  VoiceMode,
  VoiceCount,
  Trigger,
  BendRange,
  Mono,
  Output,
};
constexpr size_t kPartParamCount = 14;

// Value = parameter-ID index for part 0xFF, Slot::Part. Never renumber.
enum class MasterParam : uint32_t { Level = 0, Dry, Mode, SplitNote };
constexpr size_t kMasterParamCount = 4;

enum class PartMode { Single = 0, Layer = 1, Split = 2, Multi = 3 };

constexpr size_t kParamsPerPart = kSourceParamCount + kPartParamCount;
constexpr size_t kParamCount = size_t(kNumParts) * kParamsPerPart + kMasterParamCount;

constexpr size_t sourceParamIndex(int part, int p) {
  return size_t(part) * kParamsPerPart + size_t(p);
}
constexpr size_t partParamIndex(int part, PartParam param) {
  return size_t(part) * kParamsPerPart + kSourceParamCount + size_t(uint32_t(param) - 1);
}
constexpr size_t masterParamIndex(MasterParam param) {
  return size_t(kNumParts) * kParamsPerPart + size_t(uint32_t(param));
}

enum class ParamFormat {
  Engine,
  Percent,
  Pan,
  GlideTime,
  Semitones,
  BendRange,
  Note,
  Channel,
  VoiceMode,
  VoiceCount,
  Trigger,
  OnOff,
  OutputBus,
  PartMode,
};

struct ParamSpec {
  uint32_t id;
  const char* name;
  const char* module;
  float minValue;
  float maxValue;
  float defaultValue;   // instrument
  float effectDefault;  // EG13 FX
  bool stepped;
  ParamFormat format;
};

const ParamSpec& paramSpec(size_t index);  // index < kParamCount
int paramIndex(uint32_t id);               // -1 if unknown
float paramDefault(size_t index, PluginKind kind);
float clampParam(size_t index, double value);  // clamp, round stepped, NaN -> min
void formatParam(size_t index, double value, char* buf, size_t size);
float glideSeconds(float normalized);  // 2 * v^2 seconds

}  // namespace eg13
```

- [ ] **Step 5: Replace `core/synth/param_table.cpp`**

```cpp
#include "synth/param_table.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include "engine/plaits_engine.h"
#include "params/param_ids.h"

namespace eg13 {

namespace {

const char* const kSourceModules[kNumParts] = {"Part A/Source", "Part B/Source"};
const char* const kPartModules[kNumParts] = {"Part A", "Part B"};

struct PartParamDef {
  PartParam param;
  const char* name;
  float minValue;
  float maxValue;
  float defaultValue;
  bool stepped;
  ParamFormat format;
};

const PartParamDef kPartDefs[kPartParamCount] = {
    {PartParam::Level, "Level", 0, 1, 1, false, ParamFormat::Percent},
    {PartParam::Pan, "Pan", -1, 1, 0, false, ParamFormat::Pan},
    {PartParam::Glide, "Glide", 0, 1, 0, false, ParamFormat::GlideTime},
    {PartParam::Transpose, "Transpose", -24, 24, 0, true, ParamFormat::Semitones},
    {PartParam::BaseNote, "Base Note", 0, 127, 60, true, ParamFormat::Note},
    {PartParam::MidiChannel, "MIDI Channel", 0, 16, 0, true, ParamFormat::Channel},
    {PartParam::KeyLow, "Key Low", 0, 127, 0, true, ParamFormat::Note},
    {PartParam::KeyHigh, "Key High", 0, 127, 127, true, ParamFormat::Note},
    {PartParam::VoiceMode, "Voice Mode", 0, 2, 0, true, ParamFormat::VoiceMode},
    {PartParam::VoiceCount, "Voices", 1, 8, 8, true, ParamFormat::VoiceCount},
    {PartParam::Trigger, "Trigger", 0, 1, 0, true, ParamFormat::Trigger},
    {PartParam::BendRange, "Bend Range", 0, 24, 2, true, ParamFormat::BendRange},
    {PartParam::Mono, "Mono", 0, 1, 0, true, ParamFormat::OnOff},
    {PartParam::Output, "Output", 0, 1, 0, true, ParamFormat::OutputBus},
};

const std::array<ParamSpec, kParamCount>& table() {
  static const std::array<ParamSpec, kParamCount> specs = [] {
    std::array<ParamSpec, kParamCount> t{};
    const EngineInfo& info = PlaitsEngine::staticInfo();
    for (int part = 0; part < kNumParts; ++part) {
      for (int p = 0; p < int(kSourceParamCount); ++p) {
        const float def = info.params[p].defaultValue;
        t[sourceParamIndex(part, p)] = {params::id(uint32_t(part), params::Slot::Source, uint32_t(p + 1)),
                                        info.params[p].name, kSourceModules[part], 0.0f, 1.0f, def, def,
                                        false, ParamFormat::Engine};
      }
      for (const PartParamDef& d : kPartDefs) {
        t[partParamIndex(part, d.param)] = {params::id(uint32_t(part), params::Slot::Part, uint32_t(d.param)),
                                            d.name, kPartModules[part], d.minValue, d.maxValue,
                                            d.defaultValue, d.defaultValue, d.stepped, d.format};
      }
    }
    auto master = [](MasterParam m) {
      return params::id(params::kMasterPart, params::Slot::Part, uint32_t(m));
    };
    t[masterParamIndex(MasterParam::Level)] = {master(MasterParam::Level), "Level", "Master", 0, 1,
                                               0.8f, 0.8f, false, ParamFormat::Percent};
    t[masterParamIndex(MasterParam::Dry)] = {master(MasterParam::Dry), "Dry", "Master", 0, 1, 0, 1,
                                             false, ParamFormat::Percent};
    t[masterParamIndex(MasterParam::Mode)] = {master(MasterParam::Mode), "Mode", "Master", 0, 3, 0, 0,
                                              true, ParamFormat::PartMode};
    t[masterParamIndex(MasterParam::SplitNote)] = {master(MasterParam::SplitNote), "Split Note",
                                                   "Master", 0, 127, 60, 60, true, ParamFormat::Note};
    return t;
  }();
  return specs;
}

const char* pick(const char* const* names, int count, float value) {
  const int i = std::clamp(int(std::lround(value)), 0, count - 1);
  return names[i];
}

}  // namespace

const ParamSpec& paramSpec(size_t index) { return table()[index]; }

int paramIndex(uint32_t id) {
  const auto& t = table();
  for (size_t i = 0; i < kParamCount; ++i) {
    if (t[i].id == id) return int(i);
  }
  return -1;
}

float paramDefault(size_t index, PluginKind kind) {
  const ParamSpec& s = paramSpec(index);
  return kind == PluginKind::Effect ? s.effectDefault : s.defaultValue;
}

float clampParam(size_t index, double value) {
  const ParamSpec& s = paramSpec(index);
  if (std::isnan(value)) return s.minValue;
  double v = std::clamp(value, double(s.minValue), double(s.maxValue));
  if (s.stepped) v = std::round(v);
  return float(v);
}

float glideSeconds(float normalized) { return 2.0f * normalized * normalized; }

void formatParam(size_t index, double value, char* buf, size_t size) {
  static const char* const kNotes[12] = {"C", "C#", "D", "D#", "E", "F",
                                         "F#", "G", "G#", "A", "A#", "B"};
  static const char* const kVoiceModes[3] = {"Poly", "Mono", "Legato"};
  static const char* const kTriggers[2] = {"MIDI", "Always on"};
  static const char* const kOnOff[2] = {"Off", "On"};
  static const char* const kOutputs[2] = {"Main", "Bus 2"};
  static const char* const kModes[4] = {"Single", "Layer", "Split", "Multi"};

  const ParamSpec& s = paramSpec(index);
  const float v = clampParam(index, value);
  switch (s.format) {
    case ParamFormat::Engine:
      PlaitsEngine::staticInfo().params[index % kParamsPerPart].format(v, buf, size);
      break;
    case ParamFormat::Percent:
      std::snprintf(buf, size, "%d%%", int(std::lround(v * 100.0f)));
      break;
    case ParamFormat::Pan:
      if (v < -0.005f) {
        std::snprintf(buf, size, "L%d", int(std::lround(-v * 100.0f)));
      } else if (v > 0.005f) {
        std::snprintf(buf, size, "R%d", int(std::lround(v * 100.0f)));
      } else {
        std::snprintf(buf, size, "C");
      }
      break;
    case ParamFormat::GlideTime: {
      const float seconds = glideSeconds(v);
      if (seconds <= 0.0f) {
        std::snprintf(buf, size, "Off");
      } else if (seconds < 1.0f) {
        std::snprintf(buf, size, "%d ms", int(std::lround(seconds * 1000.0f)));
      } else {
        std::snprintf(buf, size, "%.2f s", double(seconds));
      }
      break;
    }
    case ParamFormat::Semitones:
      std::snprintf(buf, size, "%+d st", int(v));
      break;
    case ParamFormat::BendRange:
      std::snprintf(buf, size, "+/-%d st", int(v));
      break;
    case ParamFormat::Note: {
      const int note = int(v);
      std::snprintf(buf, size, "%s%d", kNotes[note % 12], note / 12 - 1);
      break;
    }
    case ParamFormat::Channel:
      if (int(v) == 0) {
        std::snprintf(buf, size, "Omni");
      } else {
        std::snprintf(buf, size, "%d", int(v));
      }
      break;
    case ParamFormat::VoiceMode:
      std::snprintf(buf, size, "%s", pick(kVoiceModes, 3, v));
      break;
    case ParamFormat::VoiceCount:
      std::snprintf(buf, size, "%d", int(v));
      break;
    case ParamFormat::Trigger:
      std::snprintf(buf, size, "%s", pick(kTriggers, 2, v));
      break;
    case ParamFormat::OnOff:
      std::snprintf(buf, size, "%s", pick(kOnOff, 2, v));
      break;
    case ParamFormat::OutputBus:
      std::snprintf(buf, size, "%s", pick(kOutputs, 2, v));
      break;
    case ParamFormat::PartMode:
      std::snprintf(buf, size, "%s", pick(kModes, 4, v));
      break;
  }
}

}  // namespace eg13
```

- [ ] **Step 6: Keep the M0 `Synth` working on the new table**

In `core/synth/synth.h`: delete the line `enum class PluginKind { Instrument, Effect };` and add `#include "synth/plugin_kind.h"` with the other includes.

In `core/synth/synth.cpp`, replace `setParam`, `resetParamsToDefaults` and the first three lines of `process`:

```cpp
bool Synth::setParam(uint32_t id, double value) {
  const int index = paramIndex(id);
  if (index < 0 || std::isnan(value)) return false;
  values_[size_t(index)].store(clampParam(size_t(index), value), std::memory_order_relaxed);
  return true;
}
```

```cpp
void Synth::resetParamsToDefaults() {
  for (size_t i = 0; i < kParamCount; ++i) {
    values_[i].store(paramDefault(i, kind_), std::memory_order_relaxed);
  }
}
```

```cpp
void Synth::process(const float* const* in, float* const* out, int n) {
  ParamValues p;
  for (int i = 0; i < int(kSourceParamCount); ++i) p.p[i] = load(sourceParamIndex(0, i));
  const float gain = load(masterParamIndex(MasterParam::Level));
```

(The M0 synth still plays Part A's Macro Oscillator only; Task 8 replaces it.)

- [ ] **Step 7: Update `tests/test_synth.cpp` for the new table**

Delete the whole `TEST_CASE("Parameter table lists Macro Oscillator P1-P8 and master level")` (Step 1's file replaces it), and in `TEST_CASE("Synth starts at default parameter values")` change the comparison line to:

```cpp
    REQUIRE(synth.param(paramSpec(i).id) == double(paramDefault(i, PluginKind::Instrument)));
```

- [ ] **Step 8: Give hosts the real ranges in `plugin/src/eg13_plugin.cpp`**

Replace `paramsGetInfo` with:

```cpp
bool paramsGetInfo(const clap_plugin_t* p, uint32_t index, clap_param_info_t* info) {
  if (index >= kParamCount) return false;
  const ParamSpec& spec = paramSpec(index);
  std::memset(info, 0, sizeof(*info));
  info->id = spec.id;
  info->flags = CLAP_PARAM_IS_AUTOMATABLE | (spec.stepped ? CLAP_PARAM_IS_STEPPED : 0u);
  info->cookie = nullptr;
  std::snprintf(info->name, sizeof(info->name), "%s", spec.name);
  std::snprintf(info->module, sizeof(info->module), "%s", spec.module);
  info->min_value = double(spec.minValue);
  info->max_value = double(spec.maxValue);
  info->default_value = double(paramDefault(index, self(p)->kind));
  return true;
}
```

- [ ] **Step 9: Run the tests and the plugin validators**

Run: `cmake --build --preset mac --target EG13_all eg13_tests && ctest --preset mac && scripts/validate.sh`
Expected: all tests pass and the script prints `All validators passed.`

- [ ] **Step 10: Commit**

```bash
git add core/synth tests/test_param_table.cpp tests/test_synth.cpp plugin/src/eg13_plugin.cpp
git commit -m "Parameter table v2: parts, master, ranges, stepping and formats"
```

---
### Task 7: Part

**Files:**
- Create: `core/part/part.h`, `core/part/part.cpp`, `tests/test_part.cpp`
- Modify: `core/CMakeLists.txt` (add `part/part.cpp`)

**Interfaces:**
- Consumes: `Voice`, `SourceSpec`, `SourceFactory`, `makePlaitsSource` (Task 5); `VoiceAllocator`, `VoiceMode`, `VoiceActions` (Task 4); `MonoNoteStack` (M0); `SmoothedValue` (Task 2); `glideSeconds` (Task 6); `ParamValues`.
- Produces:
  - `struct PartSettings { float level = 1; float pan = 0; float glide = 0; int transpose = 0; int baseNote = 60; int midiChannel = 0; int keyLow = 0; int keyHigh = 127; VoiceMode voiceMode = VoiceMode::Poly; int voiceCount = 8; bool alwaysOn = false; int bendRange = 2; bool mono = false; int output = 0; };`
  - `class Part { explicit Part(SourceFactory factory = makePlaitsSource); void prepare(double hostRate, int maxBlock); void reset(); void applySettings(const PartSettings&); const PartSettings& settings() const; bool acceptsChannel(int channel) const; bool acceptsKey(int key) const; void noteOn(int key, float velocity); void noteOff(int key); void pitchBend(float value); void sustain(bool down); void allNotesOff(); void process(float* const* out, const ParamValues& source, int n); int activeVoiceCount() const; int latencySamples() const; };`
  - Behaviour: `applySettings` is called once per block on the audio thread. A new voice mode or count releases held notes. Turning Always-on on releases everything and holds voice 0 at the base note; while Always-on, notes glide voice 0 (last-note priority) and releasing every key glides it back to the base note; turning it off releases voice 0. Pitch offset = transpose + bend × bend range. Level and pan use a balance law (`L = level·min(1, 1 − pan)`, `R = level·min(1, 1 + pan)`), smoothed over 10 ms; Mono sums the part to the centre at `level` and ignores pan. `process` overwrites `out[0..1][0..n)` and requires `n <= maxBlock`. `acceptsChannel` takes MIDI channels 0..15 (negative = any); setting 0 is Omni. `acceptsKey` accepts keys between the two key-range bounds whichever order they are in.

- [ ] **Step 1: Write the failing tests `tests/test_part.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "alloc_counter.h"
#include "part/part.h"
#include "test_engines.h"
#include "test_helpers.h"

using namespace eg13;

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

SourceSpec sine() { return {std::make_unique<test::SineEngine>(), 48000.0, 12}; }

struct Stereo {
  std::vector<float> l, r;
};

Stereo run(Part& part, int n, const ParamValues& p = ParamValues{}) {
  Stereo s{std::vector<float>(static_cast<size_t>(n)), std::vector<float>(static_cast<size_t>(n))};
  for (int done = 0; done < n;) {
    const int count = std::min(kBlock, n - done);
    float* out[2] = {s.l.data() + done, s.r.data() + done};
    part.process(out, p, count);
    done += count;
  }
  return s;
}

Part preparedSinePart() {
  Part part(sine);
  part.prepare(kRate, kBlock);
  return part;
}

}  // namespace

TEST_CASE("A chord uses one voice per key") {
  Part part = preparedSinePart();
  part.noteOn(60, 1.0f);
  part.noteOn(64, 1.0f);
  part.noteOn(67, 1.0f);
  run(part, kBlock);
  REQUIRE(part.activeVoiceCount() == 3);
}

TEST_CASE("The voice count limits polyphony") {
  Part part = preparedSinePart();
  PartSettings s;
  s.voiceCount = 2;
  part.applySettings(s);
  for (int key : {60, 64, 67}) part.noteOn(key, 1.0f);
  run(part, kBlock);
  REQUIRE(part.activeVoiceCount() == 2);
}

TEST_CASE("Released voices free up for new notes") {
  Part part = preparedSinePart();
  for (int key = 60; key < 68; ++key) part.noteOn(key, 1.0f);
  run(part, kBlock);
  for (int key = 60; key < 68; ++key) part.noteOff(key);
  run(part, 4800);
  REQUIRE(part.activeVoiceCount() == 0);
  for (int key = 70; key < 78; ++key) part.noteOn(key, 1.0f);
  run(part, kBlock);
  REQUIRE(part.activeVoiceCount() == 8);
}

TEST_CASE("Voice count change while notes are held releases them") {
  Part part = preparedSinePart();
  for (int key : {60, 64, 67}) part.noteOn(key, 1.0f);
  run(part, kBlock);
  PartSettings s;
  s.voiceCount = 4;
  part.applySettings(s);
  run(part, 4800);
  REQUIRE(part.activeVoiceCount() == 0);
}

TEST_CASE("Transpose and pitch bend move the pitch") {
  Part part = preparedSinePart();
  PartSettings s;
  s.transpose = 12;
  part.applySettings(s);
  part.noteOn(57, 1.0f);
  auto out = run(part, 48000);
  REQUIRE_THAT(test::estimateFrequency(out.l, kRate, 4800, 48000),
               Catch::Matchers::WithinRel(440.0, 0.001));

  s.transpose = 0;
  part.applySettings(s);
  part.pitchBend(1.0f);  // bend range 2
  out = run(part, 48000);
  REQUIRE_THAT(test::estimateFrequency(out.l, kRate, 4800, 48000),
               Catch::Matchers::WithinRel(220.0 * std::pow(2.0, 2.0 / 12.0), 0.001));
}

TEST_CASE("Pan follows a balance law and Mono centres the part") {
  Part part = preparedSinePart();
  PartSettings s;
  s.pan = -1.0f;
  part.applySettings(s);
  part.noteOn(60, 1.0f);
  auto out = run(part, 9600);
  REQUIRE(test::rms(out.r, 4800, 9600) < 1e-6);
  REQUIRE(test::rms(out.l, 4800, 9600) > 0.1);

  s.mono = true;
  part.applySettings(s);
  out = run(part, 9600);
  for (size_t i = 4800; i < 9600; ++i) REQUIRE(out.l[i] == out.r[i]);
  REQUIRE(test::rms(out.r, 4800, 9600) > 0.1);
}

TEST_CASE("Level zero silences the part") {
  Part part = preparedSinePart();
  part.noteOn(60, 1.0f);
  PartSettings s;
  s.level = 0.0f;
  part.applySettings(s);
  const auto out = run(part, 9600);
  for (size_t i = 4800; i < 9600; ++i) REQUIRE(out.l[i] == 0.0f);
}

TEST_CASE("Always-on plays the base note with no MIDI and follows held keys") {
  Part part = preparedSinePart();
  PartSettings s;
  s.alwaysOn = true;
  s.baseNote = 69;
  part.applySettings(s);
  auto out = run(part, 48000);
  REQUIRE(part.activeVoiceCount() == 1);
  REQUIRE_THAT(test::estimateFrequency(out.l, kRate, 4800, 48000),
               Catch::Matchers::WithinRel(440.0, 0.001));

  part.noteOn(81, 1.0f);
  out = run(part, 48000);
  REQUIRE(part.activeVoiceCount() == 1);
  REQUIRE_THAT(test::estimateFrequency(out.l, kRate, 4800, 48000),
               Catch::Matchers::WithinRel(880.0, 0.001));

  part.noteOff(81);
  out = run(part, 48000);
  REQUIRE_THAT(test::estimateFrequency(out.l, kRate, 4800, 48000),
               Catch::Matchers::WithinRel(440.0, 0.001));

  s.alwaysOn = false;
  part.applySettings(s);
  run(part, 9600);
  REQUIRE(part.activeVoiceCount() == 0);
}

TEST_CASE("Channel and key filters") {
  Part part = preparedSinePart();
  REQUIRE(part.acceptsChannel(0));
  REQUIRE(part.acceptsChannel(15));
  PartSettings s;
  s.midiChannel = 2;
  s.keyLow = 72;
  s.keyHigh = 48;  // reversed bounds still mean 48..72
  part.applySettings(s);
  REQUIRE(part.acceptsChannel(1));
  REQUIRE_FALSE(part.acceptsChannel(0));
  REQUIRE(part.acceptsChannel(-1));
  REQUIRE(part.acceptsKey(48));
  REQUIRE(part.acceptsKey(72));
  REQUIRE_FALSE(part.acceptsKey(47));
  REQUIRE_FALSE(part.acceptsKey(73));
}

TEST_CASE("Sustain keeps released notes sounding until pedal up") {
  Part part = preparedSinePart();
  part.sustain(true);
  part.noteOn(60, 1.0f);
  run(part, kBlock);
  part.noteOff(60);
  run(part, 9600);
  REQUIRE(part.activeVoiceCount() == 1);
  part.sustain(false);
  run(part, 9600);
  REQUIRE(part.activeVoiceCount() == 0);
}

TEST_CASE("The default Plaits part makes sound") {
  Part part;
  part.prepare(44100.0, kBlock);
  part.noteOn(60, 1.0f);
  const auto out = run(part, 22050);
  REQUIRE(test::rms(out.l, 4410, 22050) > 0.01);
}

TEST_CASE("Part processing does not allocate") {
  Part part;
  part.prepare(44100.0, kBlock);
  std::vector<float> l(kBlock), r(kBlock);
  float* out[2] = {l.data(), r.data()};
  const ParamValues p;
  part.process(out, p, kBlock);
  PartSettings s;
  test::ScopedAllocationCounter counter;
  for (int block = 0; block < 200; ++block) {
    if (block == 10) for (int key : {60, 64, 67, 71}) part.noteOn(key, 0.8f);
    if (block == 50) part.pitchBend(0.5f);
    if (block == 60) {
      s.voiceCount = 3;
      part.applySettings(s);
    }
    if (block == 100) {
      s.alwaysOn = true;
      part.applySettings(s);
    }
    part.process(out, p, kBlock);
  }
  REQUIRE(counter.count() == 0);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build --preset mac --target eg13_tests`
Expected: compile error, `'part/part.h' file not found`.

- [ ] **Step 3: Write `core/part/part.h`**

```cpp
#pragma once

#include <memory>
#include <vector>

#include "util/smoothed_value.h"
#include "voice/mono_note_stack.h"
#include "voice/source_factory.h"
#include "voice/voice.h"
#include "voice/voice_allocator.h"

namespace eg13 {

struct PartSettings {
  float level = 1.0f;
  float pan = 0.0f;
  float glide = 0.0f;  // normalized; see glideSeconds()
  int transpose = 0;
  int baseNote = 60;
  int midiChannel = 0;  // 0 = Omni, 1..16
  int keyLow = 0;
  int keyHigh = 127;
  VoiceMode voiceMode = VoiceMode::Poly;
  int voiceCount = 8;
  bool alwaysOn = false;
  int bendRange = 2;
  bool mono = false;
  int output = 0;  // 0 = main, 1 = Bus 2
};

// One part: 8 voices, their allocator and the part's settings.
class Part {
 public:
  static constexpr int kVoices = VoiceAllocator::kMaxVoices;

  explicit Part(SourceFactory factory = makePlaitsSource);

  void prepare(double hostRate, int maxBlock);  // main thread
  void reset();
  void applySettings(const PartSettings& settings);  // once per block
  const PartSettings& settings() const { return settings_; }

  bool acceptsChannel(int channel) const;  // 0..15, negative = any
  bool acceptsKey(int key) const;

  void noteOn(int key, float velocity);
  void noteOff(int key);
  void pitchBend(float value);  // -1..1
  void sustain(bool down);
  void allNotesOff();

  // Overwrites out[0], out[1] with n samples (n <= maxBlock).
  void process(float* const* out, const ParamValues& source, int n);

  int activeVoiceCount() const;
  int latencySamples() const { return voices_[0]->latencySamples(); }

 private:
  void perform(const VoiceActions& actions);
  void updatePitchOffset();
  void updateGains();

  std::vector<std::unique_ptr<Voice>> voices_;
  VoiceAllocator allocator_;
  PartSettings settings_;
  MonoNoteStack alwaysOnNotes_;
  bool alwaysOn_ = false;
  float bend_ = 0.0f;
  SmoothedValue gainL_;
  SmoothedValue gainR_;
  std::vector<float> mix_[2];
};

}  // namespace eg13
```

- [ ] **Step 4: Write `core/part/part.cpp`**

```cpp
#include "part/part.h"

#include <algorithm>

#include "synth/param_table.h"

namespace eg13 {

namespace {
constexpr double kGainSmoothingSeconds = 0.01;
}  // namespace

Part::Part(SourceFactory factory) {
  for (int v = 0; v < kVoices; ++v) voices_.push_back(std::make_unique<Voice>(factory()));
}

void Part::prepare(double hostRate, int maxBlock) {
  for (auto& voice : voices_) voice->prepare(hostRate, maxBlock);
  for (auto& buffer : mix_) buffer.assign(size_t(maxBlock), 0.0f);
  gainL_.prepare(hostRate, kGainSmoothingSeconds);
  gainR_.prepare(hostRate, kGainSmoothingSeconds);
  reset();
}

void Part::reset() {
  for (auto& voice : voices_) voice->reset();
  allocator_.allNotesOff();
  for (int v = 0; v < kVoices; ++v) allocator_.voiceIdle(v);
  alwaysOnNotes_.clear();
  alwaysOn_ = false;
  bend_ = 0.0f;
  const PartSettings current = settings_;
  settings_ = PartSettings{};
  allocator_.configure(settings_.voiceMode, settings_.voiceCount);
  applySettings(current);
  gainL_.reset(gainL_.target());
  gainR_.reset(gainR_.target());
}

void Part::applySettings(const PartSettings& s) {
  const bool configChanged =
      s.voiceMode != settings_.voiceMode || s.voiceCount != settings_.voiceCount;
  const bool alwaysOnChanged = s.alwaysOn != alwaysOn_;
  const bool baseChanged = s.baseNote != settings_.baseNote;
  settings_ = s;

  if (configChanged) perform(allocator_.configure(s.voiceMode, s.voiceCount));
  if (alwaysOnChanged) {
    if (s.alwaysOn) {
      perform(allocator_.allNotesOff());
      alwaysOnNotes_.clear();
      alwaysOn_ = true;
      voices_[0]->trigger(s.baseNote, 1.0f);
    } else {
      alwaysOn_ = false;
      alwaysOnNotes_.clear();
      voices_[0]->release();
    }
  } else if (alwaysOn_ && baseChanged && alwaysOnNotes_.empty()) {
    voices_[0]->glideTo(s.baseNote);
  }

  const float glide = glideSeconds(s.glide);
  for (auto& voice : voices_) voice->setGlideSeconds(glide);
  updatePitchOffset();
  updateGains();
}

bool Part::acceptsChannel(int channel) const {
  return settings_.midiChannel == 0 || channel < 0 || channel == settings_.midiChannel - 1;
}

bool Part::acceptsKey(int key) const {
  const int low = std::min(settings_.keyLow, settings_.keyHigh);
  const int high = std::max(settings_.keyLow, settings_.keyHigh);
  return key >= low && key <= high;
}

void Part::noteOn(int key, float velocity) {
  if (alwaysOn_) {
    alwaysOnNotes_.noteOn(key, velocity);
    voices_[0]->glideTo(key);
    return;
  }
  perform(allocator_.noteOn(key, velocity));
}

void Part::noteOff(int key) {
  if (alwaysOn_) {
    const bool wasTop = !alwaysOnNotes_.empty() && alwaysOnNotes_.topKey() == key;
    alwaysOnNotes_.noteOff(key);
    if (alwaysOnNotes_.empty()) {
      voices_[0]->glideTo(settings_.baseNote);
    } else if (wasTop) {
      voices_[0]->glideTo(alwaysOnNotes_.topKey());
    }
    return;
  }
  perform(allocator_.noteOff(key));
}

void Part::pitchBend(float value) {
  bend_ = std::clamp(value, -1.0f, 1.0f);
  updatePitchOffset();
}

void Part::sustain(bool down) {
  if (!alwaysOn_) perform(allocator_.setSustain(down));
}

void Part::allNotesOff() {
  if (alwaysOn_) {
    alwaysOnNotes_.clear();
    voices_[0]->glideTo(settings_.baseNote);
    return;
  }
  perform(allocator_.allNotesOff());
}

void Part::process(float* const* out, const ParamValues& source, int n) {
  std::fill(mix_[0].begin(), mix_[0].begin() + n, 0.0f);
  std::fill(mix_[1].begin(), mix_[1].begin() + n, 0.0f);
  float* mix[2] = {mix_[0].data(), mix_[1].data()};
  for (int v = 0; v < kVoices; ++v) {
    voices_[size_t(v)]->process(mix, source, n);
    if (allocator_.sounding(v) && !voices_[size_t(v)]->active()) allocator_.voiceIdle(v);
  }
  for (int i = 0; i < n; ++i) {
    const float gl = gainL_.next();
    const float gr = gainR_.next();
    if (settings_.mono) {
      const float centre = 0.5f * (mix_[0][size_t(i)] + mix_[1][size_t(i)]) * gl;
      out[0][i] = centre;
      out[1][i] = centre;
    } else {
      out[0][i] = mix_[0][size_t(i)] * gl;
      out[1][i] = mix_[1][size_t(i)] * gr;
    }
  }
}

int Part::activeVoiceCount() const {
  int count = 0;
  for (const auto& voice : voices_) count += voice->active() ? 1 : 0;
  return count;
}

void Part::perform(const VoiceActions& actions) {
  for (int i = 0; i < actions.count; ++i) {
    const VoiceAction& a = actions.items[i];
    Voice& voice = *voices_[size_t(a.voice)];
    switch (a.type) {
      case VoiceAction::Type::Trigger:
        voice.trigger(a.key, a.velocity);
        break;
      case VoiceAction::Type::Glide:
        voice.glideTo(a.key);
        break;
      case VoiceAction::Type::Release:
        voice.release();
        break;
    }
  }
}

void Part::updatePitchOffset() {
  const float offset = float(settings_.transpose) + bend_ * float(settings_.bendRange);
  for (auto& voice : voices_) voice->setPitchOffset(offset);
}

void Part::updateGains() {
  if (settings_.mono) {
    gainL_.setTarget(settings_.level);
    gainR_.setTarget(settings_.level);
    return;
  }
  gainL_.setTarget(settings_.level * std::min(1.0f, 1.0f - settings_.pan));
  gainR_.setTarget(settings_.level * std::min(1.0f, 1.0f + settings_.pan));
}

}  // namespace eg13
```

- [ ] **Step 5: Add `part/part.cpp` to `core/CMakeLists.txt` and run the tests**

Run: `cmake --build --preset mac --target eg13_tests && ctest --preset mac`
Expected: all tests pass.

- [ ] **Step 6: Commit**

```bash
git add core/part core/CMakeLists.txt tests/test_part.cpp
git commit -m "Add Part: 8 voices, allocation, always-on, level, pan and mono"
```

---
### Task 8: Synth v2

**Files:**
- Modify (rewrite): `core/synth/synth.h`, `core/synth/synth.cpp`, `tests/test_synth.cpp`
- Modify: `core/state/state.cpp` (one call), `plugin/src/eg13_plugin.cpp` (event and process calls)

**Interfaces:**
- Consumes: `Part`, `PartSettings` (Task 7); parameter table (Task 6); `SmoothedValue` (Task 2); `SourceFactory`, `makePlaitsSource` (Task 5); `VoiceMode`.
- Produces (`core/synth/synth.h`):
  ```cpp
  using ParamSnapshot = std::array<float, kParamCount>;
  class Synth {
   public:
    explicit Synth(PluginKind kind, SourceFactory factory = makePlaitsSource);
    PluginKind kind() const;
    void prepare(double sampleRate, int maxBlock);   // main thread, not processing
    void setProcessing(bool active);                 // from start/stop_processing
    void reset();                                    // audio thread
    void noteOn(int channel, int key, float velocity);  // channel 0..15; velocity 0 = note-off
    void noteOff(int channel, int key);              // channel < 0 = every channel
    void pitchBend(int channel, float value);        // -1..1
    void controlChange(int channel, int cc, int value);  // 64 sustain; 120, 123 all notes off
    void allNotesOff();
    bool setParam(uint32_t id, double value);        // audio thread, or main thread when not processing
    double param(uint32_t id) const;                 // main thread; reflects a pending snapshot
    ParamSnapshot defaultSnapshot() const;
    ParamSnapshot snapshot() const;                  // main thread
    void loadParams(const ParamSnapshot& values);    // main thread; applied whole
    void resetParamsToDefaults();                    // main thread
    void process(const float* const* in, float* const* mainOut, float* const* bus2Out, int n);
    int latencySamples() const;
    int activeVoiceCount(int part) const;
  };
  ```
- Behaviour:
  - **Routing** (see Scope notes): a note goes to each part whose channel and key filters accept it, limited by the mode (Single: Part A only; Split: A below the split note, B from it up). The parts that received each note-on are remembered per channel and key, so its note-off reaches exactly those parts even if settings change in between.
  - **Mixing:** each part goes to the main output, or to Bus 2 when its Output is Bus 2 and `bus2Out` is not null (otherwise to main). In Single mode Part B is muted. Master level (smoothed) scales synth output on both buses; it does not scale Dry.
  - **Dry:** when `in` is not null, `mainOut += dry × input delayed by latencySamples()`. `in` may alias `mainOut`. The FX default Dry is 1.
  - **Snapshots:** `loadParams` clamps the values and, if not processing, stores them at once. While processing it publishes them, and the audio thread copies all of them at the start of the next `process` call. `param()` returns the pending value until then. If a previous snapshot is still pending, `loadParams` waits for it, or stores directly if processing has stopped.
  - **Latency:** the parts' source latency, fixed at `prepare`.
  - `process` never allocates, and requires `n <= maxBlock`.

- [ ] **Step 1: Replace `tests/test_synth.cpp` with the v2 tests**

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

#include "alloc_counter.h"
#include "params/param_ids.h"
#include "synth/param_table.h"
#include "synth/synth.h"
#include "test_engines.h"
#include "test_helpers.h"

using namespace eg13;

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

SourceSpec sine() { return {std::make_unique<test::SineEngine>(), 48000.0, 12}; }

uint32_t partId(int part, PartParam p) { return paramSpec(partParamIndex(part, p)).id; }
uint32_t masterId(MasterParam m) { return paramSpec(masterParamIndex(m)).id; }

struct Stereo {
  std::vector<float> l, r;
  explicit Stereo(size_t n) : l(n), r(n) {}
};

void run(Synth& synth, Stereo& out, size_t from, size_t to, const Stereo* in = nullptr,
         Stereo* bus2 = nullptr, int chunk = kBlock) {
  for (size_t done = from; done < to;) {
    const int count = int(std::min<size_t>(size_t(chunk), to - done));
    float* o[2] = {out.l.data() + done, out.r.data() + done};
    float* b[2] = {bus2 ? bus2->l.data() + done : nullptr, bus2 ? bus2->r.data() + done : nullptr};
    const float* i[2] = {in ? in->l.data() + done : nullptr, in ? in->r.data() + done : nullptr};
    synth.process(in ? i : nullptr, o, bus2 ? b : nullptr, count);
    done += size_t(count);
  }
}

Stereo sineInput(size_t n, double rate) {
  Stereo s(n);
  for (size_t i = 0; i < n; ++i) {
    s.l[i] = 0.5f * float(std::sin(6.283185307179586 * 220.0 * double(i) / rate));
    s.r[i] = -s.l[i];
  }
  return s;
}

}  // namespace

TEST_CASE("Synth starts at the defaults for its kind") {
  Synth instrument(PluginKind::Instrument);
  Synth fx(PluginKind::Effect);
  for (size_t i = 0; i < kParamCount; ++i) {
    REQUIRE(instrument.param(paramSpec(i).id) == double(paramDefault(i, PluginKind::Instrument)));
    REQUIRE(fx.param(paramSpec(i).id) == double(paramDefault(i, PluginKind::Effect)));
  }
  REQUIRE(fx.param(masterId(MasterParam::Dry)) == 1.0);
}

TEST_CASE("setParam clamps, rounds and rejects NaN and unknown IDs") {
  Synth synth(PluginKind::Instrument);
  REQUIRE(synth.setParam(params::kMasterLevel, 1.5));
  REQUIRE(synth.param(params::kMasterLevel) == 1.0);
  REQUIRE(synth.setParam(partId(0, PartParam::VoiceCount), 3.4));
  REQUIRE(synth.param(partId(0, PartParam::VoiceCount)) == 3.0);
  REQUIRE(synth.setParam(params::kMasterLevel, 0.25));
  REQUIRE_FALSE(synth.setParam(params::kMasterLevel, std::numeric_limits<double>::quiet_NaN()));
  REQUIRE(synth.param(params::kMasterLevel) == 0.25);
  REQUIRE_FALSE(synth.setParam(0xDEADBEEF, 0.5));
}

TEST_CASE("Instrument is silent without notes and sounds with one") {
  Synth synth(PluginKind::Instrument);
  synth.prepare(kRate, kBlock);
  Stereo out(48000);
  run(synth, out, 0, 24000);
  REQUIRE(test::rms(out.l, 0, 24000) < 1e-4);
  synth.noteOn(0, 60, 1.0f);
  run(synth, out, 24000, 48000);
  REQUIRE(test::rms(out.l, 24000, 48000) > 0.01);
}

TEST_CASE("Master level at zero mutes the synth") {
  Synth synth(PluginKind::Instrument, sine);
  synth.prepare(kRate, kBlock);
  synth.setParam(params::kMasterLevel, 0.0);
  synth.noteOn(0, 60, 1.0f);
  Stereo out(9600);
  run(synth, out, 0, 9600);
  for (size_t i = 4800; i < 9600; ++i) REQUIRE(out.l[i] == 0.0f);
}

TEST_CASE("A chord plays polyphonically on Part A") {
  Synth synth(PluginKind::Instrument, sine);
  synth.prepare(kRate, kBlock);
  for (int key : {60, 64, 67}) synth.noteOn(0, key, 1.0f);
  Stereo out(kBlock);
  run(synth, out, 0, kBlock);
  REQUIRE(synth.activeVoiceCount(0) == 3);
  REQUIRE(synth.activeVoiceCount(1) == 0);
}

TEST_CASE("Modes route notes to parts") {
  Stereo out(kBlock);
  SECTION("Layer sends every note to both parts") {
    Synth synth(PluginKind::Instrument, sine);
    synth.setParam(masterId(MasterParam::Mode), double(PartMode::Layer));
    synth.prepare(kRate, kBlock);
    synth.noteOn(0, 60, 1.0f);
    run(synth, out, 0, kBlock);
    REQUIRE(synth.activeVoiceCount(0) == 1);
    REQUIRE(synth.activeVoiceCount(1) == 1);
  }
  SECTION("Split sends low keys to A and high keys to B") {
    Synth synth(PluginKind::Instrument, sine);
    synth.setParam(masterId(MasterParam::Mode), double(PartMode::Split));
    synth.setParam(masterId(MasterParam::SplitNote), 60);
    synth.prepare(kRate, kBlock);
    synth.noteOn(0, 48, 1.0f);
    synth.noteOn(0, 60, 1.0f);
    synth.noteOn(0, 72, 1.0f);
    run(synth, out, 0, kBlock);
    REQUIRE(synth.activeVoiceCount(0) == 1);
    REQUIRE(synth.activeVoiceCount(1) == 2);
  }
  SECTION("Multi sends each channel to its part") {
    Synth synth(PluginKind::Instrument, sine);
    synth.setParam(masterId(MasterParam::Mode), double(PartMode::Multi));
    synth.setParam(partId(0, PartParam::MidiChannel), 1);
    synth.setParam(partId(1, PartParam::MidiChannel), 2);
    synth.prepare(kRate, kBlock);
    synth.noteOn(0, 60, 1.0f);
    synth.noteOn(1, 64, 1.0f);
    synth.noteOn(1, 67, 1.0f);
    synth.noteOn(5, 70, 1.0f);  // neither part listens to channel 6
    run(synth, out, 0, kBlock);
    REQUIRE(synth.activeVoiceCount(0) == 1);
    REQUIRE(synth.activeVoiceCount(1) == 2);
  }
  SECTION("Single mode ignores Part B") {
    Synth synth(PluginKind::Instrument, sine);
    synth.prepare(kRate, kBlock);
    synth.noteOn(0, 60, 1.0f);
    run(synth, out, 0, kBlock);
    REQUIRE(synth.activeVoiceCount(1) == 0);
  }
}

TEST_CASE("Note-off follows the note-on routing after settings change") {
  Synth synth(PluginKind::Instrument, sine);
  synth.setParam(masterId(MasterParam::Mode), double(PartMode::Split));
  synth.setParam(masterId(MasterParam::SplitNote), 60);
  synth.prepare(kRate, kBlock);
  Stereo out(9600);
  synth.noteOn(0, 55, 1.0f);  // Part A
  run(synth, out, 0, kBlock);
  synth.setParam(masterId(MasterParam::SplitNote), 50);  // 55 would now go to Part B
  run(synth, out, kBlock, 2 * kBlock);
  synth.noteOff(0, 55);
  run(synth, out, 2 * kBlock, 9600);
  REQUIRE(synth.activeVoiceCount(0) == 0);
}

TEST_CASE("Bus 2 carries parts routed to it, and falls back to main when not connected") {
  Synth synth(PluginKind::Instrument, sine);
  synth.setParam(partId(0, PartParam::Output), 1);
  synth.prepare(kRate, kBlock);
  synth.noteOn(0, 60, 1.0f);
  Stereo main(4800), bus2(4800);
  run(synth, main, 0, 4800, nullptr, &bus2);
  REQUIRE(test::rms(main.l, 0, 4800) == 0.0);
  REQUIRE(test::rms(bus2.l, 480, 4800) > 0.1);

  Stereo fallback(4800);
  run(synth, fallback, 0, 4800);
  REQUIRE(test::rms(fallback.l, 480, 4800) > 0.1);
}

TEST_CASE("FX passes its input through, delayed by the reported latency") {
  for (double rate : {44100.0, 48000.0}) {
    Synth synth(PluginKind::Effect);
    synth.prepare(rate, kBlock);
    const int latency = synth.latencySamples();
    const Stereo in = sineInput(8820, rate);
    Stereo out(8820);
    run(synth, out, 0, 8820, &in);
    INFO("rate " << rate << " latency " << latency);
    for (size_t i = size_t(latency); i < 8820; ++i) {
      REQUIRE(std::fabs(out.l[i] - in.l[i - size_t(latency)]) < 1e-4f);
      REQUIRE(std::fabs(out.r[i] - in.r[i - size_t(latency)]) < 1e-4f);
    }
  }
}

TEST_CASE("FX passthrough survives in-place buffers") {
  Synth synth(PluginKind::Effect);
  synth.prepare(kRate, kBlock);
  const size_t latency = size_t(synth.latencySamples());
  const Stereo original = sineInput(4800, kRate);
  Stereo buffer = original;
  for (size_t done = 0; done < 4800; done += 240) {
    float* io[2] = {buffer.l.data() + done, buffer.r.data() + done};
    synth.process(io, io, nullptr, 240);
  }
  for (size_t i = latency; i < 4800; ++i) {
    REQUIRE(std::fabs(buffer.l[i] - original.l[i - latency]) < 1e-4f);
    REQUIRE(std::fabs(buffer.r[i] - original.r[i - latency]) < 1e-4f);
  }
}

TEST_CASE("Instrument ignores input when Dry is 0") {
  Synth synth(PluginKind::Instrument);
  synth.prepare(kRate, kBlock);
  const Stereo in = sineInput(4800, kRate);
  Stereo out(4800);
  run(synth, out, 0, 4800, &in);
  REQUIRE(test::rms(out.l, 0, 4800) < 1e-4);
}

TEST_CASE("loadParams while processing is applied whole at the next block") {
  Synth synth(PluginKind::Instrument, sine);
  synth.prepare(kRate, kBlock);
  synth.setProcessing(true);
  ParamSnapshot values = synth.defaultSnapshot();
  values[masterParamIndex(MasterParam::Level)] = 0.5f;
  values[partParamIndex(0, PartParam::VoiceCount)] = 2.0f;
  synth.loadParams(values);
  REQUIRE(synth.param(params::kMasterLevel) == 0.5);  // visible before the audio thread runs
  REQUIRE(synth.param(partId(0, PartParam::VoiceCount)) == 2.0);
  Stereo out(kBlock);
  run(synth, out, 0, kBlock);
  for (int key : {60, 64, 67}) synth.noteOn(0, key, 1.0f);
  run(synth, out, 0, kBlock);
  REQUIRE(synth.activeVoiceCount(0) == 2);
  REQUIRE(synth.param(params::kMasterLevel) == 0.5);
  synth.setProcessing(false);
}

TEST_CASE("loadParams when not processing applies immediately") {
  Synth synth(PluginKind::Instrument);
  ParamSnapshot values = synth.defaultSnapshot();
  values[masterParamIndex(MasterParam::Level)] = 0.3f;
  values[partParamIndex(1, PartParam::VoiceCount)] = 99.0f;  // clamped
  synth.loadParams(values);
  REQUIRE(synth.param(params::kMasterLevel) == double(0.3f));
  REQUIRE(synth.param(partId(1, PartParam::VoiceCount)) == 8.0);
  REQUIRE(synth.snapshot()[masterParamIndex(MasterParam::Level)] == 0.3f);
}

TEST_CASE("Mono voice mode returns to the previous held note") {
  Synth synth(PluginKind::Instrument);
  synth.setParam(partId(0, PartParam::VoiceMode), double(VoiceMode::Mono));
  synth.prepare(kRate, kBlock);
  Stereo out(96000);
  synth.noteOn(0, 60, 1.0f);
  run(synth, out, 0, 24000);
  synth.noteOn(0, 67, 1.0f);
  run(synth, out, 24000, 48000);
  synth.noteOff(0, 67);
  run(synth, out, 48000, 96000);
  REQUIRE_THAT(test::estimateFrequency(out.l, kRate, 30000, 48000),
               Catch::Matchers::WithinRel(392.0, 0.01));
  REQUIRE_THAT(test::estimateFrequency(out.l, kRate, 60000, 96000),
               Catch::Matchers::WithinRel(261.63, 0.01));
  REQUIRE(synth.activeVoiceCount(0) == 1);
}

TEST_CASE("Pitch bend and the sustain pedal reach the part") {
  Synth synth(PluginKind::Instrument, sine);
  synth.prepare(kRate, kBlock);
  Stereo out(48000);
  synth.noteOn(0, 57, 1.0f);
  synth.pitchBend(0, 1.0f);  // +2 semitones
  run(synth, out, 0, 48000);
  REQUIRE_THAT(test::estimateFrequency(out.l, kRate, 4800, 48000),
               Catch::Matchers::WithinRel(220.0 * std::pow(2.0, 2.0 / 12.0), 0.001));

  synth.controlChange(0, 64, 127);
  synth.noteOff(0, 57);
  run(synth, out, 0, 9600);
  REQUIRE(synth.activeVoiceCount(0) == 1);
  synth.controlChange(0, 64, 0);
  run(synth, out, 0, 9600);
  REQUIRE(synth.activeVoiceCount(0) == 0);
}

TEST_CASE("All notes off (CC 123) silences held notes") {
  Synth synth(PluginKind::Instrument, sine);
  synth.prepare(kRate, kBlock);
  Stereo out(9600);
  for (int key : {60, 64}) synth.noteOn(0, key, 1.0f);
  run(synth, out, 0, kBlock);
  synth.controlChange(0, 123, 0);
  run(synth, out, 0, 9600);
  REQUIRE(synth.activeVoiceCount(0) == 0);
}

TEST_CASE("An Always-on part sounds with no MIDI") {
  Synth synth(PluginKind::Effect, sine);
  synth.setParam(partId(0, PartParam::Trigger), 1);
  synth.setParam(masterId(MasterParam::Dry), 0.0);
  synth.prepare(kRate, kBlock);
  Stereo out(9600);
  run(synth, out, 0, 9600);
  REQUIRE(synth.activeVoiceCount(0) == 1);
  REQUIRE(test::rms(out.l, 4800, 9600) > 0.1);
}

TEST_CASE("Event timing is independent of block slicing") {
  auto renderWith = [](int chunk) {
    Synth synth(PluginKind::Instrument);
    synth.prepare(44100.0, 4096);
    Stereo out(9000);
    run(synth, out, 0, 1001, nullptr, nullptr, chunk);
    synth.noteOn(0, 64, 0.9f);
    run(synth, out, 1001, 9000, nullptr, nullptr, chunk);
    return out.l;
  };
  const auto a = renderWith(1);
  REQUIRE(a == renderWith(7));
  REQUIRE(a == renderWith(4096));
}

TEST_CASE("Synth processing does not allocate") {
  Synth synth(PluginKind::Effect);
  synth.setParam(masterId(MasterParam::Mode), double(PartMode::Layer));
  synth.prepare(44100.0, kBlock);
  synth.setProcessing(true);
  Stereo in = sineInput(kBlock, 44100.0), out(kBlock), bus2(kBlock);
  run(synth, out, 0, kBlock, &in, &bus2);
  test::ScopedAllocationCounter counter;
  for (int block = 0; block < 200; ++block) {
    if (block == 10) for (int key : {60, 64, 67}) synth.noteOn(0, key, 0.8f);
    if (block == 40) synth.pitchBend(0, -0.5f);
    if (block == 60) synth.setParam(partId(1, PartParam::VoiceCount), 2);
    if (block == 80) synth.controlChange(0, 64, 127);
    if (block == 90) synth.noteOff(0, 64);
    if (block == 120) synth.controlChange(0, 64, 0);
    if (block == 150) synth.setParam(masterId(MasterParam::Mode), double(PartMode::Split));
    run(synth, out, 0, kBlock, &in, &bus2);
  }
  REQUIRE(counter.count() == 0);
  synth.setProcessing(false);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build --preset mac --target eg13_tests`
Expected: compile errors (`Synth` has no `noteOn(int, int, float)`, no `setProcessing`, wrong `process` arity, ...).

- [ ] **Step 3: Replace `core/synth/synth.h`**

```cpp
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "part/part.h"
#include "synth/param_table.h"
#include "synth/plugin_kind.h"
#include "util/smoothed_value.h"
#include "voice/source_factory.h"

namespace eg13 {

using ParamSnapshot = std::array<float, kParamCount>;

// Two parts, MIDI routing by mode, main and Bus 2 outputs, Dry input and
// atomic parameter snapshots.
class Synth {
 public:
  explicit Synth(PluginKind kind, SourceFactory factory = makePlaitsSource);
  Synth(const Synth&) = delete;
  Synth& operator=(const Synth&) = delete;

  PluginKind kind() const { return kind_; }

  void prepare(double sampleRate, int maxBlock);  // main thread, not processing
  void setProcessing(bool active);
  void reset();

  void noteOn(int channel, int key, float velocity);
  void noteOff(int channel, int key);  // channel < 0: every channel
  void pitchBend(int channel, float value);
  void controlChange(int channel, int cc, int value);
  void allNotesOff();

  bool setParam(uint32_t id, double value);
  double param(uint32_t id) const;
  ParamSnapshot defaultSnapshot() const;
  ParamSnapshot snapshot() const;
  void loadParams(const ParamSnapshot& values);
  void resetParamsToDefaults() { loadParams(defaultSnapshot()); }

  // in: nullable, may alias mainOut. bus2Out: nullable. n <= maxBlock.
  void process(const float* const* in, float* const* mainOut, float* const* bus2Out, int n);

  int latencySamples() const { return latency_; }
  int activeVoiceCount(int part) const { return parts_[size_t(part)]->activeVoiceCount(); }

 private:
  float load(size_t index) const { return values_[index].load(std::memory_order_relaxed); }
  void store(const ParamSnapshot& values);
  void applyPending();
  PartSettings readPartSettings(int part) const;
  static int clampChannel(int channel);

  PluginKind kind_;
  std::array<std::unique_ptr<Part>, kNumParts> parts_;
  std::array<std::atomic<float>, kParamCount> values_;
  ParamSnapshot pendingValues_{};
  std::atomic<bool> pending_{false};
  std::atomic<bool> processing_{false};
  SmoothedValue masterGain_;
  SmoothedValue dryGain_;
  int latency_ = 0;
  std::vector<float> dryDelay_[2];
  size_t dryPos_ = 0;
  std::vector<float> partOut_[kNumParts][2];
  uint8_t routed_[16][128] = {};
};

}  // namespace eg13
```

- [ ] **Step 4: Replace `core/synth/synth.cpp`**

```cpp
#include "synth/synth.h"

#include <algorithm>
#include <cmath>
#include <thread>

namespace eg13 {

namespace {
constexpr double kGainSmoothingSeconds = 0.01;
}  // namespace

Synth::Synth(PluginKind kind, SourceFactory factory) : kind_(kind) {
  for (auto& part : parts_) part = std::make_unique<Part>(factory);
  store(defaultSnapshot());
}

void Synth::prepare(double sampleRate, int maxBlock) {
  applyPending();
  for (auto& part : parts_) part->prepare(sampleRate, maxBlock);
  latency_ = parts_[0]->latencySamples();
  for (auto& line : dryDelay_) line.assign(size_t(latency_), 0.0f);
  for (auto& part : partOut_) {
    for (auto& buffer : part) buffer.assign(size_t(maxBlock), 0.0f);
  }
  masterGain_.prepare(sampleRate, kGainSmoothingSeconds);
  dryGain_.prepare(sampleRate, kGainSmoothingSeconds);
  reset();
}

void Synth::setProcessing(bool active) { processing_.store(active, std::memory_order_release); }

void Synth::reset() {
  for (int p = 0; p < kNumParts; ++p) {
    parts_[size_t(p)]->applySettings(readPartSettings(p));
    parts_[size_t(p)]->reset();
  }
  for (auto& line : dryDelay_) std::fill(line.begin(), line.end(), 0.0f);
  dryPos_ = 0;
  for (auto& channel : routed_) std::fill(std::begin(channel), std::end(channel), uint8_t{0});
  masterGain_.reset(load(masterParamIndex(MasterParam::Level)));
  dryGain_.reset(load(masterParamIndex(MasterParam::Dry)));
}

int Synth::clampChannel(int channel) { return std::clamp(channel, 0, 15); }

void Synth::noteOn(int channel, int key, float velocity) {
  if (key < 0 || key > 127) return;
  channel = clampChannel(channel);
  if (velocity <= 0.0f) {
    noteOff(channel, key);
    return;
  }
  const auto mode = PartMode(int(load(masterParamIndex(MasterParam::Mode))));
  const int split = int(load(masterParamIndex(MasterParam::SplitNote)));
  uint8_t mask = 0;
  for (int p = 0; p < kNumParts; ++p) {
    Part& part = *parts_[size_t(p)];
    if (mode == PartMode::Single && p != 0) continue;
    if (!part.acceptsChannel(channel) || !part.acceptsKey(key)) continue;
    if (mode == PartMode::Split && ((p == 0) != (key < split))) continue;
    part.noteOn(key, velocity);
    mask = uint8_t(mask | (1u << p));
  }
  routed_[channel][key] = uint8_t(routed_[channel][key] | mask);
}

void Synth::noteOff(int channel, int key) {
  if (key < 0 || key > 127) return;
  const int first = channel < 0 ? 0 : clampChannel(channel);
  const int last = channel < 0 ? 15 : first;
  for (int ch = first; ch <= last; ++ch) {
    const uint8_t mask = routed_[ch][key];
    for (int p = 0; p < kNumParts; ++p) {
      if (mask & (1u << p)) parts_[size_t(p)]->noteOff(key);
    }
    routed_[ch][key] = 0;
  }
}

void Synth::pitchBend(int channel, float value) {
  for (auto& part : parts_) {
    if (part->acceptsChannel(channel)) part->pitchBend(value);
  }
}

void Synth::controlChange(int channel, int cc, int value) {
  if (cc == 64) {
    for (auto& part : parts_) {
      if (part->acceptsChannel(channel)) part->sustain(value >= 64);
    }
  } else if (cc == 120 || cc == 123) {
    for (auto& part : parts_) {
      if (part->acceptsChannel(channel)) part->allNotesOff();
    }
    if (channel >= 0) {
      std::fill(std::begin(routed_[clampChannel(channel)]), std::end(routed_[clampChannel(channel)]),
                uint8_t{0});
    }
  }
}

void Synth::allNotesOff() {
  for (auto& part : parts_) part->allNotesOff();
  for (auto& channel : routed_) std::fill(std::begin(channel), std::end(channel), uint8_t{0});
}

bool Synth::setParam(uint32_t id, double value) {
  const int index = paramIndex(id);
  if (index < 0 || std::isnan(value)) return false;
  values_[size_t(index)].store(clampParam(size_t(index), value), std::memory_order_relaxed);
  return true;
}

double Synth::param(uint32_t id) const {
  const int index = paramIndex(id);
  if (index < 0) return 0.0;
  if (pending_.load(std::memory_order_acquire)) return double(pendingValues_[size_t(index)]);
  return double(load(size_t(index)));
}

ParamSnapshot Synth::defaultSnapshot() const {
  ParamSnapshot values{};
  for (size_t i = 0; i < kParamCount; ++i) values[i] = paramDefault(i, kind_);
  return values;
}

ParamSnapshot Synth::snapshot() const {
  ParamSnapshot values{};
  const bool pending = pending_.load(std::memory_order_acquire);
  for (size_t i = 0; i < kParamCount; ++i) values[i] = pending ? pendingValues_[i] : load(i);
  return values;
}

void Synth::store(const ParamSnapshot& values) {
  for (size_t i = 0; i < kParamCount; ++i) values_[i].store(values[i], std::memory_order_relaxed);
}

void Synth::loadParams(const ParamSnapshot& values) {
  ParamSnapshot clamped{};
  for (size_t i = 0; i < kParamCount; ++i) clamped[i] = clampParam(i, double(values[i]));

  // Wait for the audio thread to take a previous snapshot.
  while (pending_.load(std::memory_order_acquire) && processing_.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  if (!processing_.load(std::memory_order_acquire)) {
    store(clamped);
    pending_.store(false, std::memory_order_release);
    return;
  }
  pendingValues_ = clamped;
  pending_.store(true, std::memory_order_release);
}

void Synth::applyPending() {
  if (!pending_.load(std::memory_order_acquire)) return;
  store(pendingValues_);
  pending_.store(false, std::memory_order_release);
}

PartSettings Synth::readPartSettings(int part) const {
  auto v = [this, part](PartParam p) { return load(partParamIndex(part, p)); };
  PartSettings s;
  s.level = v(PartParam::Level);
  s.pan = v(PartParam::Pan);
  s.glide = v(PartParam::Glide);
  s.transpose = int(v(PartParam::Transpose));
  s.baseNote = int(v(PartParam::BaseNote));
  s.midiChannel = int(v(PartParam::MidiChannel));
  s.keyLow = int(v(PartParam::KeyLow));
  s.keyHigh = int(v(PartParam::KeyHigh));
  s.voiceMode = VoiceMode(int(v(PartParam::VoiceMode)));
  s.voiceCount = int(v(PartParam::VoiceCount));
  s.alwaysOn = v(PartParam::Trigger) >= 0.5f;
  s.bendRange = int(v(PartParam::BendRange));
  s.mono = v(PartParam::Mono) >= 0.5f;
  s.output = int(v(PartParam::Output));
  return s;
}

void Synth::process(const float* const* in, float* const* mainOut, float* const* bus2Out, int n) {
  applyPending();
  for (int p = 0; p < kNumParts; ++p) parts_[size_t(p)]->applySettings(readPartSettings(p));
  masterGain_.setTarget(load(masterParamIndex(MasterParam::Level)));
  dryGain_.setTarget(load(masterParamIndex(MasterParam::Dry)));
  const auto mode = PartMode(int(load(masterParamIndex(MasterParam::Mode))));

  bool toBus2[kNumParts];
  for (int p = 0; p < kNumParts; ++p) {
    ParamValues source;
    for (int i = 0; i < int(kSourceParamCount); ++i) source.p[i] = load(sourceParamIndex(p, i));
    float* out[2] = {partOut_[p][0].data(), partOut_[p][1].data()};
    parts_[size_t(p)]->process(out, source, n);
    toBus2[p] = bus2Out != nullptr && parts_[size_t(p)]->settings().output == 1;
  }

  for (int i = 0; i < n; ++i) {
    float main[2] = {0.0f, 0.0f};
    float bus[2] = {0.0f, 0.0f};
    for (int p = 0; p < kNumParts; ++p) {
      if (mode == PartMode::Single && p != 0) continue;
      float* target = toBus2[p] ? bus : main;
      target[0] += partOut_[p][0][size_t(i)];
      target[1] += partOut_[p][1][size_t(i)];
    }
    const float gain = masterGain_.next();
    const float dry = dryGain_.next();
    for (int ch = 0; ch < 2; ++ch) {
      float delayed = 0.0f;
      if (in != nullptr) {
        const float input = in[ch][i];  // read before mainOut is written (may alias)
        if (latency_ == 0) {
          delayed = input;
        } else {
          delayed = dryDelay_[ch][dryPos_];
          dryDelay_[ch][dryPos_] = input;
        }
      }
      mainOut[ch][i] = main[ch] * gain + delayed * dry;
      if (bus2Out != nullptr) bus2Out[ch][i] = bus[ch] * gain;
    }
    if (in != nullptr && latency_ > 0) dryPos_ = (dryPos_ + 1) % size_t(latency_);
  }
}

}  // namespace eg13
```

- [ ] **Step 5: Update the callers**

`core/state/state.cpp` still uses `setParam` and `resetParamsToDefaults`, which keep their signatures, so no change is needed there. Task 9 rewrites it.

`plugin/src/eg13_plugin.cpp`:

1. In `handleEvent`, pass channels and handle MIDI 1 pitch bend and controllers. Replace the cases for `CLAP_EVENT_NOTE_ON`, `CLAP_EVENT_NOTE_OFF`/`CLAP_EVENT_NOTE_CHOKE` and `CLAP_EVENT_MIDI` with:

```cpp
    case CLAP_EVENT_NOTE_ON: {
      const auto* ev = reinterpret_cast<const clap_event_note_t*>(hdr);
      if (ev->key >= 0 && ev->key <= 127) {
        plugin->synth.noteOn(ev->channel < 0 ? 0 : ev->channel, ev->key, float(ev->velocity));
      }
      break;
    }
    case CLAP_EVENT_NOTE_OFF:
    case CLAP_EVENT_NOTE_CHOKE: {
      const auto* ev = reinterpret_cast<const clap_event_note_t*>(hdr);
      if (ev->key < 0) {
        plugin->synth.allNotesOff();
      } else {
        plugin->synth.noteOff(ev->channel, ev->key);
      }
      break;
    }
```

```cpp
    case CLAP_EVENT_MIDI: {
      const auto* ev = reinterpret_cast<const clap_event_midi_t*>(hdr);
      const uint8_t status = ev->data[0] & 0xF0;
      const int channel = ev->data[0] & 0x0F;
      const int d1 = ev->data[1] & 0x7F;
      const int d2 = ev->data[2] & 0x7F;
      if (status == 0x90 && d2 > 0) {
        plugin->synth.noteOn(channel, d1, float(d2) / 127.0f);
      } else if (status == 0x80 || status == 0x90) {
        plugin->synth.noteOff(channel, d1);
      } else if (status == 0xB0) {
        plugin->synth.controlChange(channel, d1, d2);
      } else if (status == 0xE0) {
        plugin->synth.pitchBend(channel, float(((d2 << 7) | d1) - 8192) / 8192.0f);
      }
      break;
    }
```

2. In `pluginProcess`, change both `plugin->synth.process(...)` calls to pass `nullptr` as the new third argument (`bus2Out`), e.g. `plugin->synth.process(inSlice, outSlice, nullptr, int(next - frame));`. Task 10 adds the real Bus 2 port.

3. Make `pluginStartProcessing` call `self(p)->synth.setProcessing(true)` and return true, and `pluginStopProcessing` call `self(p)->synth.setProcessing(false)`.

- [ ] **Step 6: Run the tests and validators**

Run: `cmake --build --preset mac --target EG13_all eg13_tests && ctest --preset mac && scripts/validate.sh`
Expected: all tests pass and `All validators passed.`

- [ ] **Step 7: Commit**

```bash
git add core/synth tests/test_synth.cpp plugin/src/eg13_plugin.cpp
git commit -m "Synth v2: two parts, mode routing, Bus 2, Dry and atomic snapshots"
```

---
### Task 9: State v2

**Files:**
- Modify (rewrite): `core/state/state.h`, `core/state/state.cpp`, `tests/test_state.cpp`
- Modify: `plugin/src/eg13_plugin.cpp` (use a `StateStore` per plugin instance)

**Interfaces:**
- Consumes: `Synth::param`, `Synth::defaultSnapshot`, `Synth::loadParams`, `Synth::setActive` (Task 8; replaces the plan's setProcessing — see the ledger ruling); `paramSpec`, `paramIndex`, `kParamCount` (Task 6).
- Produces:
  ```cpp
  namespace eg13::state {
  constexpr int kSchemaVersion = 2;
  enum class LoadResult { Ok, OkWithWarnings, Invalid };
  class StateStore {   // main thread only
   public:
    std::string save(const Synth& synth) const;
    LoadResult load(Synth& synth, const std::string& json);
  };
  }
  ```
  The free functions `state::save` / `state::load` are removed.
- Behaviour:
  - Document: `{"schemaVersion":2,"params":{"<decimal id>":<number>,...},...}`.
  - Schema 1 (M0) documents load unchanged: their nine parameters kept their IDs and ranges, so migrating 1 → 2 needs no transformation.
  - `load` validates the whole document before touching the synth; `Invalid` leaves the synth and the preserved data unchanged.
  - Parameters missing from the document get the synth's kind-specific defaults.
  - Unknown parameter entries (decimal-ID keys with numeric values) and unknown top-level keys are kept and written back by the next `save` (spec §9), until a later successful `load` replaces them. Other malformed entries are dropped with a warning.
  - A newer `schemaVersion` loads with `OkWithWarnings`.
  - Values are applied through `Synth::loadParams`, so a load while audio runs switches every parameter at once.

- [ ] **Step 1: Replace `tests/test_state.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "params/param_ids.h"
#include "state/state.h"
#include "synth/param_table.h"
#include "synth/synth.h"

using namespace eg13;

namespace {
const uint32_t kModel = params::id(0, params::Slot::Source, 1);
const uint32_t kTimbre = params::id(0, params::Slot::Source, 3);
uint32_t partId(int part, PartParam p) { return paramSpec(partParamIndex(part, p)).id; }
uint32_t masterId(MasterParam m) { return paramSpec(masterParamIndex(m)).id; }
}  // namespace

TEST_CASE("State round-trips every parameter exactly") {
  Synth a(PluginKind::Instrument);
  a.setParam(kModel, 0.9);
  a.setParam(kTimbre, 0.123456789);
  a.setParam(partId(1, PartParam::VoiceCount), 3);
  a.setParam(partId(0, PartParam::Pan), -0.4);
  a.setParam(masterId(MasterParam::Mode), 2);
  state::StateStore store;
  const std::string json = store.save(a);

  Synth b(PluginKind::Instrument);
  state::StateStore other;
  REQUIRE(other.load(b, json) == state::LoadResult::Ok);
  for (size_t i = 0; i < kParamCount; ++i) {
    REQUIRE(b.param(paramSpec(i).id) == a.param(paramSpec(i).id));
  }
}

TEST_CASE("Saved state is schema 2 with decimal parameter IDs") {
  Synth a(PluginKind::Instrument);
  const std::string json = state::StateStore{}.save(a);
  REQUIRE(json.find("\"schemaVersion\":2") != std::string::npos);
  REQUIRE(json.find("\"257\":") != std::string::npos);       // Part A Source P1
  REQUIRE(json.find("\"16711680\":") != std::string::npos);  // master level
}

TEST_CASE("Schema 1 documents from M0 load unchanged") {
  Synth s(PluginKind::Instrument);
  state::StateStore store;
  REQUIRE(store.load(s, R"({"schemaVersion":1,"params":{"257":0.9,"16711680":0.5}})") ==
          state::LoadResult::Ok);
  REQUIRE(s.param(kModel) == double(0.9f));
  REQUIRE(s.param(params::kMasterLevel) == 0.5);
  REQUIRE(store.save(s).find("\"schemaVersion\":2") != std::string::npos);
}

TEST_CASE("Unusable state is rejected and leaves the synth unchanged") {
  Synth s(PluginKind::Instrument);
  s.setParam(kTimbre, 0.25);
  state::StateStore store;
  for (const char* bad : {"", "not json", "[]", "{}", R"({"schemaVersion":2})",
                          R"({"schemaVersion":"2","params":{}})",
                          R"({"schemaVersion":0,"params":{}})",
                          R"({"schemaVersion":2,"params":[]})"}) {
    INFO(bad);
    REQUIRE(store.load(s, bad) == state::LoadResult::Invalid);
    REQUIRE(s.param(kTimbre) == 0.25);
  }
}

TEST_CASE("Missing parameters fall back to the defaults for the synth's kind") {
  Synth fx(PluginKind::Effect);
  fx.setParam(masterId(MasterParam::Dry), 0.2);
  state::StateStore store;
  REQUIRE(store.load(fx, R"({"schemaVersion":2,"params":{"257":0.9}})") == state::LoadResult::Ok);
  REQUIRE(fx.param(masterId(MasterParam::Dry)) == 1.0);
  REQUIRE(fx.param(kTimbre) == double(paramDefault(sourceParamIndex(0, 2), PluginKind::Effect)));
}

TEST_CASE("Unknown data is preserved on resave") {
  Synth s(PluginKind::Instrument);
  state::StateStore store;
  const char* json =
      R"({"schemaVersion":2,"params":{"257":0.9,"999999":0.5},"future":{"x":1}})";
  REQUIRE(store.load(s, json) == state::LoadResult::OkWithWarnings);
  const std::string saved = store.save(s);
  REQUIRE(saved.find("\"999999\":0.5") != std::string::npos);
  REQUIRE(saved.find("\"future\":{\"x\":1}") != std::string::npos);

  REQUIRE(store.load(s, R"({"schemaVersion":2,"params":{}})") == state::LoadResult::Ok);
  const std::string resaved = store.save(s);
  REQUIRE(resaved.find("999999") == std::string::npos);
  REQUIRE(resaved.find("future") == std::string::npos);
}

TEST_CASE("Malformed entries are skipped with a warning") {
  Synth s(PluginKind::Instrument);
  state::StateStore store;
  const char* json = R"({"schemaVersion":2,"params":{"257":0.9,"abc":0.5,"259":"x"}})";
  REQUIRE(store.load(s, json) == state::LoadResult::OkWithWarnings);
  REQUIRE(s.param(kModel) == double(0.9f));
  REQUIRE(s.param(kTimbre) == double(paramDefault(sourceParamIndex(0, 2), PluginKind::Instrument)));
  REQUIRE(store.save(s).find("abc") == std::string::npos);
}

TEST_CASE("Newer schema versions load what is understood, with a warning") {
  Synth s(PluginKind::Instrument);
  state::StateStore store;
  REQUIRE(store.load(s, R"({"schemaVersion":7,"params":{"257":0.9}})") ==
          state::LoadResult::OkWithWarnings);
  REQUIRE(s.param(kModel) == double(0.9f));
}

TEST_CASE("Loaded values are clamped and stepped values rounded") {
  Synth s(PluginKind::Instrument);
  state::StateStore store;
  const std::string json = R"({"schemaVersion":2,"params":{"259":1.7,")" +
                           std::to_string(partId(0, PartParam::VoiceCount)) + R"(":5.6}})";
  REQUIRE(store.load(s, json) == state::LoadResult::Ok);
  REQUIRE(s.param(kTimbre) == 1.0);
  REQUIRE(s.param(partId(0, PartParam::VoiceCount)) == 6.0);
}

TEST_CASE("A load while processing is reflected immediately") {
  Synth s(PluginKind::Instrument);
  s.prepare(48000.0, 256);
  s.setActive(true);
  state::StateStore store;
  REQUIRE(store.load(s, R"({"schemaVersion":2,"params":{"16711680":0.25}})") ==
          state::LoadResult::Ok);
  REQUIRE(s.param(params::kMasterLevel) == 0.25);
  s.setActive(false);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build --preset mac --target eg13_tests`
Expected: compile error, `no type named 'StateStore' in namespace 'eg13::state'`.

- [ ] **Step 3: Replace `core/state/state.h`**

```cpp
#pragma once

#include <string>

namespace eg13 {
class Synth;
}

namespace eg13::state {

constexpr int kSchemaVersion = 2;

enum class LoadResult { Ok, OkWithWarnings, Invalid };

// Saves and loads a synth's state as JSON. Keeps data it does not understand
// (unknown parameters and top-level keys) and writes it back on save.
// Main thread only; one store per plugin instance.
class StateStore {
 public:
  std::string save(const Synth& synth) const;
  // Invalid leaves the synth and the preserved data unchanged.
  LoadResult load(Synth& synth, const std::string& json);

 private:
  std::string extraTopLevel_ = "{}";
  std::string extraParams_ = "{}";
};

}  // namespace eg13::state
```

- [ ] **Step 4: Replace `core/state/state.cpp`**

```cpp
#include "state/state.h"

#include <cstdint>
#include <cstdlib>
#include <utility>

#include <nlohmann/json.hpp>

#include "synth/param_table.h"
#include "synth/synth.h"

namespace eg13::state {

namespace {

bool parseId(const std::string& key, uint32_t* id) {
  if (key.empty() || key.size() > 10) return false;
  for (char c : key) {
    if (c < '0' || c > '9') return false;
  }
  const unsigned long long value = std::strtoull(key.c_str(), nullptr, 10);
  if (value > 0xFFFFFFFFull) return false;
  *id = uint32_t(value);
  return true;
}

// Schema 1 (M0) -> 2: the nine M0 parameters kept their IDs and ranges, so no
// transformation is needed. Later schema changes add their migrations here.

}  // namespace

std::string StateStore::save(const Synth& synth) const {
  nlohmann::json root = nlohmann::json::parse(extraTopLevel_);
  nlohmann::json params = nlohmann::json::parse(extraParams_);
  for (size_t i = 0; i < kParamCount; ++i) {
    const uint32_t id = paramSpec(i).id;
    params[std::to_string(id)] = synth.param(id);
  }
  root["schemaVersion"] = kSchemaVersion;
  root["params"] = std::move(params);
  return root.dump();
}

LoadResult StateStore::load(Synth& synth, const std::string& json) {
  const nlohmann::json root = nlohmann::json::parse(json, nullptr, false);
  if (root.is_discarded() || !root.is_object()) return LoadResult::Invalid;

  const auto version = root.find("schemaVersion");
  const auto params = root.find("params");
  if (version == root.end() || !version->is_number_integer() || params == root.end() ||
      !params->is_object()) {
    return LoadResult::Invalid;
  }
  const long long schema = version->get<long long>();
  if (schema < 1) return LoadResult::Invalid;
  bool warnings = schema > kSchemaVersion;

  ParamSnapshot values = synth.defaultSnapshot();
  nlohmann::json extraParams = nlohmann::json::object();
  for (auto it = params->begin(); it != params->end(); ++it) {
    uint32_t id = 0;
    if (!parseId(it.key(), &id) || !it.value().is_number()) {
      warnings = true;
      continue;
    }
    const int index = paramIndex(id);
    if (index < 0) {
      extraParams[it.key()] = it.value();
      warnings = true;
      continue;
    }
    values[size_t(index)] = float(it.value().get<double>());
  }

  nlohmann::json extraTopLevel = nlohmann::json::object();
  for (auto it = root.begin(); it != root.end(); ++it) {
    if (it.key() != "schemaVersion" && it.key() != "params") {
      extraTopLevel[it.key()] = it.value();
      warnings = true;
    }
  }

  synth.loadParams(values);
  extraTopLevel_ = extraTopLevel.dump();
  extraParams_ = extraParams.dump();
  return warnings ? LoadResult::OkWithWarnings : LoadResult::Ok;
}

}  // namespace eg13::state
```

(`Synth::loadParams` clamps and rounds, which covers the clamping test.)

- [ ] **Step 5: Use a `StateStore` per plugin in `plugin/src/eg13_plugin.cpp`**

Add `state::StateStore store;` as the last member of `struct Plugin`. In `stateSave` replace `state::save(self(p)->synth)` with `self(p)->store.save(self(p)->synth)`, and in `stateLoad` replace `state::load(plugin->synth, data)` with `plugin->store.load(plugin->synth, data)`. Indent both `try` bodies by two more spaces while there.

- [ ] **Step 6: Run the tests and validators**

Run: `cmake --build --preset mac --target EG13_all eg13_tests && ctest --preset mac && scripts/validate.sh`
Expected: all tests pass and `All validators passed.`

- [ ] **Step 7: Commit**

```bash
git add core/state tests/test_state.cpp plugin/src/eg13_plugin.cpp
git commit -m "State v2: schema 2, atomic loads and preserved unknown data"
```

---
### Task 10: Plugin v2: ports, latency, AU metadata rebuild

**Files:**
- Modify: `plugin/src/eg13_plugin.cpp`, `plugin/CMakeLists.txt`, `README.md`

**Interfaces:**
- Consumes: `Synth::process(in, mainOut, bus2Out, n)`, `Synth::latencySamples()`, `Synth::setActive`, `Synth::beginBlock` (Task 8); `state::StateStore` (Task 9).
- Produces (CLAP-visible; permanent once released):
  - **EG13 (instrument):** input port index 0, id 0, `"Sidechain"`, stereo, flags 0 (not main). Output port index 0, id 1, `"Output"`, stereo, main. Output port index 1, id 2, `"Bus 2"`, stereo, flags 0.
  - **EG13 FX:** input index 0, id 0, `"Input"`, stereo, main. Output index 0, id 1, `"Output"`, stereo, main.
  - `CLAP_EXT_LATENCY`: `get()` returns `synth.latencySamples()`. `activate` calls `host_latency->changed()` when the value differs from the last one reported (CLAP allows latency changes only during activate).
  - The AU `Info.plist` is regenerated whenever the CLAP binary changes.

- [ ] **Step 1: Make the AU metadata depend on the CLAP binary (`plugin/CMakeLists.txt`)**

Append:

```cmake
# clap-wrapper generates the AU Info.plist by loading the built .clap, but its
# helper target only has an order-only dependency on it. Relink the helper
# whenever the .clap changes so the plist never goes stale.
if(TARGET EG13_auv2-build-helper)
  set_property(TARGET EG13_auv2-build-helper APPEND PROPERTY LINK_DEPENDS $<TARGET_FILE:EG13_clap>)
endif()
```

Verify:

```bash
cmake --preset mac >/dev/null && cmake --build --preset mac --target EG13_all >/dev/null
P=build/mac/plugins/EG13.component/Contents/Info.plist
before=$(stat -f %m "$P"); sleep 1
touch plugin/src/descriptors.cpp && cmake --build --preset mac --target EG13_all >/dev/null
after=$(stat -f %m "$P"); echo "$before -> $after"
```
Expected: `after` is greater than `before`. If CMake rejects the generator expression in `LINK_DEPENDS`, use the literal path `${CMAKE_BINARY_DIR}/plugins/EG13.clap/Contents/MacOS/EG13` instead. If `EG13_auv2-build-helper` is not the helper's target name, find it with `cmake --build --preset mac --target help | grep build-helper` and use that name.

- [ ] **Step 2: Replace the audio-ports functions in `plugin/src/eg13_plugin.cpp`**

```cpp
uint32_t audioPortsCount(const clap_plugin_t* p, bool isInput) {
  const bool instrument = self(p)->kind == PluginKind::Instrument;
  if (isInput) return 1;                 // instrument: sidechain; FX: main input
  return instrument ? 2 : 1;             // instrument: main + Bus 2
}

bool audioPortsGet(const clap_plugin_t* p, uint32_t index, bool isInput,
                   clap_audio_port_info_t* info) {
  if (index >= audioPortsCount(p, isInput)) return false;
  const bool instrument = self(p)->kind == PluginKind::Instrument;
  const char* name = "Output";
  uint32_t flags = CLAP_AUDIO_PORT_IS_MAIN;
  clap_id id = 1;
  if (isInput) {
    id = 0;
    name = instrument ? "Sidechain" : "Input";
    flags = instrument ? 0u : uint32_t(CLAP_AUDIO_PORT_IS_MAIN);
  } else if (index == 1) {
    id = 2;
    name = "Bus 2";
    flags = 0;
  }
  info->id = id;
  std::snprintf(info->name, sizeof(info->name), "%s", name);
  info->flags = flags;
  info->channel_count = 2;
  info->port_type = CLAP_PORT_STEREO;
  info->in_place_pair = CLAP_INVALID_ID;
  return true;
}
```

- [ ] **Step 3: Add the latency extension**

Add to `struct Plugin` (before `Synth synth;`): `const clap_host_latency_t* hostLatency = nullptr; uint32_t reportedLatency = 0;`

In `pluginInit`, after `hostParams` is fetched:

```cpp
  plugin->hostLatency = static_cast<const clap_host_latency_t*>(
      plugin->host->get_extension(plugin->host, CLAP_EXT_LATENCY));
```

Replace `pluginActivate`:

```cpp
bool pluginActivate(const clap_plugin_t* p, double sampleRate, uint32_t, uint32_t maxFrames) {
  Plugin* plugin = self(p);
  plugin->synth.prepare(sampleRate, int(maxFrames));
  const uint32_t latency = uint32_t(plugin->synth.latencySamples());
  if (latency != plugin->reportedLatency) {
    plugin->reportedLatency = latency;
    if (plugin->hostLatency) plugin->hostLatency->changed(plugin->host);
  }
  return true;
}
```

Add above the `// ---- plugin` section:

```cpp
// ---- latency ----------------------------------------------------------------------

uint32_t latencyGet(const clap_plugin_t* p) { return uint32_t(self(p)->synth.latencySamples()); }

const clap_plugin_latency_t kLatency = {latencyGet};
```

In `pluginGetExtension`, add `if (std::strcmp(id, CLAP_EXT_LATENCY) == 0) return &kLatency;`.

- [ ] **Step 4: Replace the buffer handling in `pluginProcess`**

Replace everything from `const clap_audio_buffer_t& outBuf = proc->audio_outputs[0];` down to (not including) `uint32_t eventIndex = 0;` with:

```cpp
  const clap_audio_buffer_t& outBuf = proc->audio_outputs[0];
  float* out[2] = {outBuf.data32[0],
                   outBuf.channel_count > 1 ? outBuf.data32[1] : outBuf.data32[0]};

  // Bus 2 (instrument only); absent or unconnected buses fall back to main.
  float* bus2[2] = {nullptr, nullptr};
  const bool hasBus2 = plugin->kind == PluginKind::Instrument && proc->audio_outputs_count > 1 &&
                       proc->audio_outputs[1].data32 != nullptr &&
                       proc->audio_outputs[1].channel_count > 0;
  if (hasBus2) {
    const clap_audio_buffer_t& b = proc->audio_outputs[1];
    bus2[0] = b.data32[0];
    bus2[1] = b.channel_count > 1 ? b.data32[1] : b.data32[0];
  }

  // Input: the FX's main input or the instrument's sidechain (port 0 for both).
  const float* in[2] = {nullptr, nullptr};
  const bool hasInput = proc->audio_inputs_count > 0 && proc->audio_inputs[0].data32 != nullptr &&
                        proc->audio_inputs[0].channel_count > 0;
  if (hasInput) {
    const clap_audio_buffer_t& inBuf = proc->audio_inputs[0];
    in[0] = inBuf.data32[0];
    in[1] = inBuf.channel_count > 1 ? inBuf.data32[1] : inBuf.data32[0];
  }
```

And replace the slice-rendering `if (hasInput) { ... } else { ... }` inside the event loop with:

```cpp
    const int count = int(next - frame);
    float* outSlice[2] = {out[0] + frame, out[1] + frame};
    float* bus2Slice[2] = {hasBus2 ? bus2[0] + frame : nullptr, hasBus2 ? bus2[1] + frame : nullptr};
    const float* inSlice[2] = {hasInput ? in[0] + frame : nullptr, hasInput ? in[1] + frame : nullptr};
    plugin->synth.process(hasInput ? inSlice : nullptr, outSlice, hasBus2 ? bus2Slice : nullptr,
                          count);
```

(delete the old `float* outSlice[2] = ...` line that preceded it).

- [ ] **Step 5: README notes**

In `README.md`, under "Building (macOS)", add after the validate.sh paragraph:

```markdown
After installing or rebuilding the AU, quit and reopen Logic so it rescans
plugins; if EG13 is still missing, use Logic Pro → Settings → Plug-in Manager →
Reset & Rescan Selection.

EG13 reports about 2–3 ms of latency at host rates other than 48 kHz (engines
run at their native rate and are resampled); hosts compensate automatically.
```

Change the Status paragraph's first sentence to: `**Status:** M1 (synth architecture): two parts with 8 voices each, Single/Layer/Split/Multi modes, sidechain/FX input with Dry level, and a second output bus on the instrument.`

- [ ] **Step 6: Build, test, validate**

Run: `cmake --build --preset mac --target EG13_all eg13_tests && ctest --preset mac && scripts/validate.sh`
Expected: all tests pass; clap-validator 0 failures for both plugins; auval `AU VALIDATION SUCCEEDED` for `aumu EG13 Symb` and `aumf EGfx Symb`; pluginval `SUCCESS`; `All validators passed.`

Also confirm the AU buses and latency: `auval -v aumu EG13 Symb 2>&1 | grep -A3 "Input Scope Bus Configuration"` shows `Default Bus Count:1`, and `auval -v aumu EG13 Symb 2>&1 | grep -i latency` shows a latency greater than 0 at 44.1 kHz.

- [ ] **Step 7: Manual host checks (the human does these after the branch is built; the implementer skips this step and lists it in the report)**

1. Logic: EG13 on a software-instrument track plays chords (8 voices); in the generic view set Part A Voice Mode to Mono and Voices to 1; set Mode to Split and play both halves of the keyboard (set Part B's parameters to sound different, e.g. Model).
2. Logic: EG13 FX on an audio track passes the audio through (Dry 100%); set Part A Trigger to Always on and Dry to 0 → a drone at Base Note.
3. Reaper (VST3 and CLAP): EG13 makes sound with nothing routed to its sidechain and with Bus 2 unused. clap-wrapper's VST3 layer skips processing when a host gives an aux bus zero channels, so this check matters.
4. Save, reopen and confirm settings persist.

- [ ] **Step 8: Commit**

```bash
git add plugin README.md
git commit -m "Plugin v2: sidechain and Bus 2 ports, latency reporting, AU plist rebuild"
```

---
