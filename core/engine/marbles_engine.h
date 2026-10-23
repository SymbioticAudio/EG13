#pragma once

#include <memory>

#include "engine/mod_engine.h"

namespace eg13 {

// Marbles' T and X/Y random generators (marbles/random/t_generator.h, x_y_generator.h), shown as
// "Random / S&H". Runs at the module's 32 kHz in its 5-sample blocks with VCV's knob mappings
// (AudibleInstruments Marbles.cpp:440-534). Seven outputs: T1, T2, T3 (gates, 0 or 1), X1, X2, X3,
// Y (voltages scaled by X Range: 0..1 for Narrow and Positive, -1..1 for Full). Per part by
// default (spec §4).
//
// P1 T Model, P2 T Deja Vu, P3 T Rate, P4 T Bias, P5 Deja Vu, P6 X Range, P7 X Spread, P8 X Bias,
// P9 X Steps, P10 T Range, P11 T Jitter, P12 Length, P13 X Mode, P14 X Scale, P15 X Deja Vu,
// P16 Sync. Stepped lists are append-only (kModSelectorSteps). The order puts every control
// whose shared host default is 0 on an option list's first entry, and X Range (host default
// 100 %) on its last entry, Full.
class MarblesEngine final : public NativeModEngine {
 public:
  static constexpr double kNativeRate = 32000.0;  // marbles/marbles.cc:63
  static constexpr int kNativeBlock = 5;          // marbles/io_buffer.h:40
  static constexpr int kNumTModels = 3;   // the front panel's: coin toss, clusters, drums
  static constexpr int kNumTRanges = 3;   // EG13 order: 1x, 1/4x, 4x
  static constexpr int kNumXModes = 3;    // identical, bump, tilt
  static constexpr int kNumXRanges = 3;   // narrow (0..2 V), positive (0..5 V), full (+-5 V)
  static constexpr int kNumScales = 6;

  MarblesEngine();
  ~MarblesEngine() override;

  const ModEngineInfo& info() const override;
  static const ModEngineInfo& staticInfo();
  void setSeed(uint32_t seed) override { seed_ = seed; }  // applied at the next reset()

  static int lengthSteps(float normalized);  // P12: VCV's loop_length table (Marbles.cpp:445-455)
  static int tRange(float normalized);       // P10 -> marbles::TGeneratorRange

 private:
  void resetEngine() override;
  void renderBlock(const ParamValues& p, const uint8_t* gate, const double* beat, double beatsPerNative,
                   bool beatJump, float (*block)[kMaxNativeBlock]) override;

  struct Impl;
  std::unique_ptr<Impl> impl_;
  uint32_t seed_ = 1;
};

}  // namespace eg13
