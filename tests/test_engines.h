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
