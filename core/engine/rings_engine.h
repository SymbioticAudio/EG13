#pragma once

#include <memory>

#include "engine/voice_engine.h"

namespace eg13 {

// Adapter for the upstream Rings resonator, shown as "Resonator".
class RingsEngine final : public VoiceEngine {
 public:
  static constexpr double kNativeRate = 48000.0;
  static constexpr int kNativeBlock = 24;
  static constexpr int kNumModels = 6;

  RingsEngine();
  ~RingsEngine() override;

  void prepare(double hostRate, int maxBlock) override;
  void reset() override;
  void noteOn(float note, float velocity) override;
  void noteOff() override {}
  void setNote(float note) override { note_ = note; }
  void process(const float* const* in, float* const* out, const ParamValues& p,
               int n) override;
  const EngineInfo& info() const override;

  static const EngineInfo& staticInfo();
  static int modelIndex(float normalized);    // P1 -> 0..5
  static int exciterIndex(float normalized);  // P6 thirds -> 0 Internal, 1 Input, 2 Both
  static float frequencyOffset(float normalized);  // P7 -> -12..+12 semitones

 private:
  struct Impl;
  void renderBlock(const float* const* in, const ParamValues& p, float* const* out, int offset,
                   int count);

  std::unique_ptr<Impl> impl_;
  float pitchOffset_ = 0.0f;
  float note_ = 60.0f;
  float velocity_ = 1.0f;  // the Resonator responds to input before any note (FX use)
  bool strum_ = false;
  int model_ = -1;
};

}  // namespace eg13
