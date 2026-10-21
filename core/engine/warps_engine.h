#pragma once

#include <memory>

#include "engine/pot_smoother.h"
#include "engine/stereo_link.h"
#include "engine/voice_engine.h"

namespace eg13 {

// Adapter for the upstream Warps modulator, shown as "Ring Mod / Vocoder". Runs at Warps' rate
// and block size (96 kHz, 60 samples: warps.cc:91,97,100) behind a one-block FIFO, so any
// host block size works; the FIFO's 60 native samples are its nativeLatency().
// As on the module (owner, 2026-10-20, provisional): Carrier picks the CARRIER input, either
// Warps' internal oscillator (the module's button; at the voice's pitch plus Osc Pitch) or a
// signal: the voice, or side input in[2]. Modulator picks the MODULATOR input: the voice, or
// side input in[3].
class WarpsEngine final : public VoiceEngine {
 public:
  static constexpr double kNativeRate = 96000.0;
  static constexpr int kNativeBlock = 60;
  // Named positions of the Algorithm knob, every eighth: six cross-modulations
  // (modulator.cc:438-445), then the vocoder with a lengthening release (modulator.cc:314-318)
  // that freezes fully clockwise (vocoder.cc:75).
  static constexpr int kNumAlgorithms = 9;
  static constexpr int kNumOscillatorModes = 3;  // the button's modes, carrier_shape 1..3
  // Carrier and Modulator are append-only option lists read in 8 fixed steps, so adding an
  // option never changes what a saved value means.
  //
  // The steps past the last option currently read as the last option (a host may save any
  // value, 1.0 included). RULE: when an option is appended, the state-schema migration of that
  // version must snap every saved value above the old last option's step back to the old last
  // option's centre (optionValue(old last)), so saved routing keeps its meaning.
  static constexpr int kSelectorSteps = 8;
  static constexpr int kNumCarriers = 5;    // Internal Osc, Voice, Source AUX, Input, Other part
  static constexpr int kNumModulators = 4;  // Voice, Source AUX, Input, Other part
  // EG13 ±1.0 = ±5 V into Warps' ±8 V inputs, as VCV converts (Warps.cpp:102-103): 10240 counts.
  // Outputs are read back with the same scale, so a unity path through Warps is unity in EG13.
  // This departs from VCV, which reads outputs at 32768 counts = 5 V (Warps.cpp:104-105): EG13's
  // Shaper can therefore emit up to +-3.2 (32768 / 10240), about 10 dB above VCV's output scale.
  static constexpr float kInputScale = 5.0f / 16.0f * 32768.0f;
  // Carrier Level and Mod Level default: unity gain, unsaturated, through Warps' amplifier for a
  // full-scale voice (measured; see test_warps_engine.cpp). Owner decision 2026-10-20.
  static constexpr float kDefaultLevel = 0.47f;

  WarpsEngine();
  ~WarpsEngine() override;

  void prepare(double hostRate, int maxBlock) override;
  void reset() override;  // audio-thread safe: memset + Init, no allocation
  void noteOn(float note, float) override { note_ = note; }
  void noteOff() override {}
  void setNote(float note) override { note_ = note; }
  void setStereo(bool stereo) override { stereo_ = stereo; }
  int nativeLatency() const override { return kNativeBlock; }
  void process(const float* const* in, float* const* out, const ParamValues& p,
               int n) override;
  const EngineInfo& info() const override;

  static const EngineInfo& staticInfo();
  static int carrierIndex(float normalized);    // P5 -> 0..4 (0 = Internal Osc)
  static int modulatorIndex(float normalized);  // P6 -> 0..3 (0 = Voice)
  static float optionValue(int index);          // the centre of option index's step
  static float oscillatorOffset(float normalized);  // P7: -24..+24 semitones
  static int oscillatorMode(float normalized);      // P8 thirds: 0..2 (carrier_shape 1..3)
  static bool shifterMode(float normalized);        // P9 halves: modulation / frequency shifter
  // Upstream reads one entry past a lookup table when a table position is exactly 1.0
  // (stmlib::Interpolate on lut_xfade_in/out with Timbre at full, modulator.cc:167,362; on
  // lut_sin + 256 with the shifter's phase shift at full, modulator.cc:115). The adapter keeps
  // Timbre and the phase shift a hair below 1 (inaudible). Not avoidable from here: the
  // frequency shifter's quadrature oscillator wraps its phase to exactly 1.0 when a negative
  // step lands within 2^-25 below 0 (quadrature_oscillator.h:70-83), e.g. at a shift of
  // exactly 0, or on the first block after reset, which ramps from 0. It then reads the entry
  // past its wavetable but weights it by exactly 0, so the output is unaffected.
  static constexpr float kMaxTablePosition = 1.0f - 1.0f / 65536.0f;
  static float tablePosition(float normalized);  // clamp01, then at most kMaxTablePosition
  static short toShort(float x);                    // EG13 sample -> Warps' 16-bit input
  static float fromShort(short x);                  // Warps' 16-bit output -> EG13 sample
  float smoothedParam(int index) const;             // test hook: pot-filtered P1..P9
  bool rightRuns() const { return link_.rightRuns(); }

 private:
  struct Impl;
  void renderBlock(const ParamValues& p);

  std::unique_ptr<Impl> impl_;
  float note_ = 60.0f;
  bool stereo_ = false;
  int fill_ = 0;      // position in the block being collected (and the block being played)
  PotSmoother pots_;  // CvScaler pot smoothing, once per native block
  StereoLink link_;
};

}  // namespace eg13
