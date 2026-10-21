#pragma once

#include <memory>

#include "engine/mod_engine.h"

namespace eg13 {

// Tides 2's poly slope generator (tides2/poly_slope_generator.h), shown as "LFO". Runs at the
// module's 62.5 kHz in 8-sample blocks, its slope/phase and frequency output modes at half rate
// as the firmware does (tides2/tides.cc:145-171). Four outputs.
//
// P1 Mode (Cycle, AD, AR), P2 Sync (Off, On), P3 Frequency (-48..+48 semitones around the range's
// root, VCV Tides2.cpp:107; with Sync on, a ratio to the beat), P4 Shape, P5 Slope, P6 Shift,
// P7 Smoothness, P8 and P9 unused, P10 Output (Phase, Gates, Amplitude, Frequency), P11 Range
// (Medium, Low). The stepped lists are EG13's own orders (append-only, kModSelectorSteps), so
// each default is the first option and a host reset of 0 selects it.
//
// The note gate is patched into TRIG (a new note restarts a cycle), except with Sync on: then
// the ramp comes from the beat position instead of TRIG and CLOCK, as the firmware's clocked
// mode does with TRIG unpatched (tides.cc:219). EG13's gate is complete at every native
// sample; the module reads TRIG every other sample (tides.cc:165-178), which only matters at
// half rate, where EG13 also reads every other sample.
class TidesEngine final : public NativeModEngine {
 public:
  static constexpr double kNativeRate = 62500.0;  // tides2/io_buffer.h:46
  static constexpr int kNativeBlock = 8;          // tides2/io_buffer.h:40
  enum Mode { Cycle = 0, Ad = 1, Ar = 2 };
  static constexpr int kNumModes = 3;
  enum Output { Phase = 0, Gates = 1, Amplitude = 2, Frequency = 3 };
  static constexpr int kNumOutputModes = 4;
  enum Range { Medium = 0, Low = 1 };
  static constexpr int kNumRanges = 2;

  TidesEngine();
  ~TidesEngine() override;

  const ModEngineInfo& info() const override;
  static const ModEngineInfo& staticInfo();

  static int modeIndex(float normalized);    // P1
  static bool syncOn(float normalized);      // P2
  static float transposition(float normalized);  // P3: -48..+48 semitones
  static int outputIndex(float normalized);  // P10
  static int rangeIndex(float normalized);   // P11
  static float rootHz(int range);            // tides.cc:102-106: 2 Hz Medium, 0.125 Hz Low
  static float frequencyFor(double hz, int range);  // the P3 value that plays hz (free running)

 private:
  void resetEngine() override;
  void renderBlock(const ParamValues& p, const uint8_t* gate, const double* beat, double beatsPerNative,
                   bool beatJump, float (*block)[kMaxNativeBlock]) override;

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace eg13
