#pragma once

#include <cstdint>
#include <memory>

#include "engine/mod_engine.h"

namespace eg13 {

// Peaks' envelope and LFO processors (peaks/modulations/multistage_envelope.h, lfo.h), shown as
// "Function Generator". One channel of the module in its full (expert) control mode, at its
// 48 kHz in 4-sample blocks. One output: 0..1 for the envelope, -1..1 for the LFOs.
//
// P1 Mode (Envelope, LFO, Tap LFO, Random), P2 Attack, P3 Rate, P4 Decay, P5 Wave Shape,
// P6 Sustain, P7 Phase, P8 Release, P9 Smoothness, P10 unused, P11 Waveform (Sine, Triangle,
// Square, Steps, Noise). Each mode keeps its own controls, at the same positions as the
// Envelope engine's ADSR (Attack P2, Decay P4, Sustain P6, Release P8), so one set of routes
// suits both. Stepped lists are append-only (kModSelectorSteps).
class PeaksEngine final : public NativeModEngine {
 public:
  static constexpr double kNativeRate = 48000.0;  // peaks/peaks.cc:79
  static constexpr int kNativeBlock = 4;          // peaks/io_buffer.h:41
  enum Mode { Envelope = 0, Lfo = 1, TapLfo = 2, Random = 3 };
  static constexpr int kNumModes = 4;
  static constexpr int kNumWaveforms = 5;  // peaks::LfoShape order
  static constexpr uint16_t kLfoLevel = 40960;  // the free LFO's level (lfo.h Configure)

  PeaksEngine();
  ~PeaksEngine() override;

  const ModEngineInfo& info() const override;
  static const ModEngineInfo& staticInfo();

  static int modeIndex(float normalized);      // P1
  static int waveformIndex(float normalized);  // P11
  static uint16_t knob(float normalized);      // 0..1 -> the module's 0..65535 pot value
  static uint16_t waveformValue(int waveform); // the pot value set_shape_integer reads as it
  // The four pot values Configure() gets in this mode (processors.h CopyParameters).
  static void knobs(int mode, const ParamValues& p, uint16_t out[4]);

 private:
  void resetEngine() override;
  void renderBlock(const ParamValues& p, const uint8_t* gate, const double* beat, double beatsPerNative,
                   bool beatJump, float (*block)[kMaxNativeBlock]) override;

  struct Impl;
  std::unique_ptr<Impl> impl_;
  int mode_ = -1;
};

}  // namespace eg13
