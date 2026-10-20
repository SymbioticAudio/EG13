#pragma once

#include <memory>

#include "engine/voice_engine.h"

namespace eg13 {

// Adapter for the upstream Braids macro oscillator, shown as "Digital Oscillator".
class BraidsEngine final : public VoiceEngine {
 public:
  static constexpr double kNativeRate = 96000.0;
  static constexpr int kNativeBlock = 24;
  static constexpr int kNumShapes = 47;

  BraidsEngine();
  ~BraidsEngine() override;

  void prepare(double hostRate, int maxBlock) override;
  void reset() override;
  void noteOn(float note, float velocity) override;
  void noteOff() override;
  void setNote(float note) override { note_ = note; }
  void process(const float* const* in, float* const* out, const ParamValues& p,
               int n) override;
  const EngineInfo& info() const override;

  static const EngineInfo& staticInfo();
  static int shapeIndex(float normalized);       // P1 -> 0..46
  static int bitDepthIndex(float normalized);    // P6 -> 0..6 (2..16 bit)
  static int sampleRateIndex(float normalized);  // P7 -> 0..6 (4..96 kHz)
  static float frequencyOffset(float normalized);  // P4 -> -12..+12 semitones

 private:
  struct Impl;
  void renderBlock(const ParamValues& p, float* const* out, int offset, int count);

  std::unique_ptr<Impl> impl_;
  float pitchOffset_ = 0.0f;
  float note_ = 60.0f;
  float velocity_ = 0.0f;
  bool gate_ = false;
  bool strike_ = false;
  float gain_ = 0.0f;
};

}  // namespace eg13
