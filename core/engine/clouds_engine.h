#pragma once

#include <memory>

#include "engine/voice_engine.h"

namespace eg13 {

// Part FX "Granular": Clouds (clouds::GranularProcessor) as the VCV port drives it
// (AudibleInstruments/src/Clouds.cpp:139-197), at its native 32 kHz in 32-sample blocks
// (clouds/dsp/frame.h:37). P1 Blend, P2 Position, P3 Size, P4 Pitch, P5 Density, P6 Texture,
// P7 Feedback, P8 Reverb, P9 Freeze, P10 Mode, P11 Quality, P12 Spread, P13 In Gain (M5 plan
// ruling 8). Each note-on is one TRIG (ruling 13). Knobs pass straight through, as in the VCV port.
class CloudsEngine final : public VoiceEngine {
 public:
  static constexpr double kNativeRate = 32000.0;
  static constexpr int kNativeBlock = 32;
  // Mode: clouds::PlaybackMode order (Granular, Stretch, Looping delay, Spectral). Append-only.
  static constexpr int kNumModes = 4;
  // Quality: set_quality order (granular_processor.h:125-128): 16-bit stereo, 16-bit mono,
  // 8-bit stereo, 8-bit mono. Append-only.
  static constexpr int kNumQualities = 4;
  // A Mode or Quality change fades out over 10 ms, applies, then fades in over 10 ms (preflight
  // ruling G10), in native samples.
  static constexpr int kChangeFadeSamples = 320;

  CloudsEngine();
  ~CloudsEngine() override;

  void prepare(double hostRate, int maxBlock) override;
  void reset() override;
  void noteOn(float note, float velocity) override;
  void noteOff() override {}
  void setNote(float) override {}
  void process(const float* const* in, float* const* out, const ParamValues& p, int n) override;
  const EngineInfo& info() const override;

  static const EngineInfo& staticInfo();
  static float pitchSemitones(float p);  // (2P - 1) x 24 (VCV: +-2 V x 12, ruling 12)
  static float inputGain(float p);       // 2P: 50 % maps EG13's +-1.0 to int16 full scale (ruling 11)
  static int modeIndex(float p);         // P10 in 16 fixed steps
  static int qualityIndex(float p);      // P11 in 16 fixed steps

 private:
  struct Impl;
  void renderBlock(const float* const* in, float* const* out, const ParamValues& p, int offset, int count);

  std::unique_ptr<Impl> impl_;
};

}  // namespace eg13
