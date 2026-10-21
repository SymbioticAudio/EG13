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
    static const EngineInfo kInfo = {"impulse", "Impulse", 0, nullptr, false};
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
    static const EngineInfo kInfo = {"sine", "Sine", 0, nullptr, false};
    return kInfo;
  }

 private:
  double rate_ = 48000.0;
  double phase_ = 0.0;
  float note_ = 60.0f;
  bool gate_ = false;
};

// Outputs NaN for its first rendered block after its first noteOn (a one-off
// glitch that survives reset()), then behaves as a 0.5 sine.
class NanOnceEngine final : public VoiceEngine {
 public:
  void prepare(double rate, int block) override { sine_.prepare(rate, block); }
  void reset() override { sine_.reset(); }
  void noteOn(float note, float velocity) override {
    sine_.noteOn(note, velocity);
    if (!fired_) pending_ = true;
  }
  void noteOff() override { sine_.noteOff(); }
  void setNote(float note) override { sine_.setNote(note); }
  void process(const float* const* in, float* const* out, const ParamValues& p, int n) override {
    if (pending_) {
      pending_ = false;
      fired_ = true;
      for (int i = 0; i < n; ++i) out[0][i] = out[1][i] = std::nanf("");
      return;
    }
    sine_.process(in, out, p, n);
  }
  const EngineInfo& info() const override {
    static const EngineInfo kInfo = {"nanonce", "NaN once", 0, nullptr, false};
    return kInfo;
  }

 private:
  SineEngine sine_;
  bool pending_ = false;
  bool fired_ = false;
};

// Copies its input to its output (zeros when there is no input).
class EchoEngine final : public VoiceEngine {
 public:
  void prepare(double, int) override {}
  void reset() override {}
  void noteOn(float, float) override {}
  void noteOff() override {}
  void setNote(float) override {}
  void process(const float* const* in, float* const* out, const ParamValues&, int n) override {
    for (int i = 0; i < n; ++i) {
      out[0][i] = in ? in[0][i] : 0.0f;
      out[1][i] = in ? in[1][i] : 0.0f;
    }
  }
  const EngineInfo& info() const override {
    static const EngineInfo kInfo = {"echo", "Echo", 0, nullptr, true};
    return kInfo;
  }
};

// Records the maxBlock given to prepare() and the largest block given to process().
class MaxBlockProbeEngine final : public VoiceEngine {
 public:
  void prepare(double, int maxBlock) override { preparedMax = maxBlock; }
  void reset() override {}
  void noteOn(float, float) override {}
  void noteOff() override {}
  void setNote(float) override {}
  void process(const float* const*, float* const* out, const ParamValues&, int n) override {
    if (n > largest) largest = n;
    for (int i = 0; i < n; ++i) out[0][i] = out[1][i] = 0.0f;
  }
  const EngineInfo& info() const override {
    static const EngineInfo kInfo = {"probe", "Probe", 0, nullptr, false};
    return kInfo;
  }
  int preparedMax = 0;
  int largest = 0;
};

// Uses input and two side inputs: out L = side 1, out R = side 2 (zeros when absent). P1 and P2
// pick each side input's source (SideSource quarters).
class SideEchoEngine final : public VoiceEngine {
 public:
  void prepare(double, int) override {}
  void reset() override {}
  void noteOn(float, float) override {}
  void noteOff() override {}
  void setNote(float) override {}
  void process(const float* const* in, float* const* out, const ParamValues&, int n) override {
    for (int i = 0; i < n; ++i) {
      out[0][i] = in && in[2] ? in[2][i] : 0.0f;
      out[1][i] = in && in[3] ? in[3][i] : 0.0f;
    }
  }
  static SideSource source(int input, const ParamValues& p) { return sideSourceOf(p.p[input]); }
  const EngineInfo& info() const override {
    static const EngineInfo kInfo = {"sideecho", "Side echo", 0, nullptr, true, false, 2, source};
    return kInfo;
  }
};

// A source with an AUX output: one 1.0 sample into AUX (when enabled) after each note-on; L/R silent.
class AuxImpulseEngine final : public VoiceEngine {
 public:
  void prepare(double, int) override {}
  void reset() override { pending_ = false; }
  void noteOn(float, float) override { pending_ = true; }
  void noteOff() override {}
  void setNote(float) override {}
  void setAuxEnabled(bool on) override { aux_ = on; }
  void process(const float* const*, float* const* out, const ParamValues&, int n) override {
    for (int i = 0; i < n; ++i) {
      out[0][i] = 0.0f;
      out[1][i] = 0.0f;
      if (aux_) out[2][i] = pending_ ? 1.0f : 0.0f;
      pending_ = false;
    }
  }
  const EngineInfo& info() const override {
    static const EngineInfo kInfo = {"auximpulse", "AUX impulse", 0, nullptr, false, true, 0, nullptr};
    return kInfo;
  }

 private:
  bool aux_ = false;
  bool pending_ = false;
};

// Delays input L by 60 native samples on both outputs and says so through nativeLatency().
class FixedLatencyEngine final : public VoiceEngine {
 public:
  static constexpr int kDelay = 60;
  void prepare(double, int) override { reset(); }
  void reset() override {
    for (float& s : line_) s = 0.0f;
    pos_ = 0;
  }
  void noteOn(float, float) override {}
  void noteOff() override {}
  void setNote(float) override {}
  int nativeLatency() const override { return kDelay; }
  void process(const float* const* in, float* const* out, const ParamValues&, int n) override {
    for (int i = 0; i < n; ++i) {
      const float delayed = line_[pos_];
      line_[pos_] = in ? in[0][i] : 0.0f;
      pos_ = (pos_ + 1) % kDelay;
      out[0][i] = delayed;
      out[1][i] = delayed;
    }
  }
  const EngineInfo& info() const override {
    static const EngineInfo kInfo = {"fixedlatency", "Fixed latency", 0, nullptr, true, false, 0, nullptr};
    return kInfo;
  }

 private:
  float line_[kDelay] = {};
  int pos_ = 0;
};

// Passes input through and records the last setStereo() it was given.
class StereoProbeEngine final : public VoiceEngine {
 public:
  void prepare(double, int) override {}
  void reset() override {}
  void noteOn(float, float) override {}
  void noteOff() override {}
  void setNote(float) override {}
  void setStereo(bool stereo) override {
    stereo_ = stereo;
    ++calls_;
  }
  void process(const float* const* in, float* const* out, const ParamValues&, int n) override {
    for (int i = 0; i < n; ++i) {
      out[0][i] = in ? in[0][i] : 0.0f;
      out[1][i] = in ? in[1][i] : 0.0f;
    }
  }
  const EngineInfo& info() const override {
    static const EngineInfo kInfo = {"stereoprobe", "Stereo probe", 0, nullptr, true, false, 0, nullptr};
    return kInfo;
  }
  bool stereo() const { return stereo_; }
  int calls() const { return calls_; }

 private:
  bool stereo_ = false;
  int calls_ = 0;
};

// Uses input: out = in * 4 * P1 (in nullptr -> zeros).
class GainEngine final : public VoiceEngine {
 public:
  void prepare(double, int) override {}
  void reset() override {}
  void noteOn(float, float) override {}
  void noteOff() override {}
  void setNote(float) override {}
  void process(const float* const* in, float* const* out, const ParamValues& p, int n) override {
    for (int i = 0; i < n; ++i) {
      out[0][i] = in ? in[0][i] * 4.0f * p.p[0] : 0.0f;
      out[1][i] = in ? in[1][i] * 4.0f * p.p[0] : 0.0f;
    }
  }
  const EngineInfo& info() const override {
    static const EngineParamInfo kParams[1] = {{"Gain", 0.25f, nullptr}};
    static const EngineInfo kInfo = {"gain", "Gain", 1, kParams, true, false, 0, nullptr};
    return kInfo;
  }
};

// Outputs 0.5 on both channels whatever its input: an oscillator in a processor slot.
class ConstantEngine final : public VoiceEngine {
 public:
  void prepare(double, int) override {}
  void reset() override {}
  void noteOn(float, float) override {}
  void noteOff() override {}
  void setNote(float) override {}
  void process(const float* const*, float* const* out, const ParamValues&, int n) override {
    for (int i = 0; i < n; ++i) out[0][i] = out[1][i] = 0.5f;
  }
  const EngineInfo& info() const override {
    static const EngineInfo kInfo = {"constant", "Constant", 0, nullptr, true, false, 0, nullptr};
    return kInfo;
  }
};

// A source with an AUX output: L = R = 0.25 while gated; AUX (when enabled) = 0.75.
class AuxSourceEngine final : public VoiceEngine {
 public:
  void prepare(double, int) override {}
  void reset() override { gate_ = false; }
  void noteOn(float, float) override { gate_ = true; }
  void noteOff() override { gate_ = false; }
  void setNote(float) override {}
  void setAuxEnabled(bool on) override { aux_ = on; }
  void process(const float* const*, float* const* out, const ParamValues&, int n) override {
    for (int i = 0; i < n; ++i) {
      out[0][i] = out[1][i] = gate_ ? 0.25f : 0.0f;
      if (aux_) out[2][i] = gate_ ? 0.75f : 0.0f;
    }
  }
  const EngineInfo& info() const override {
    static const EngineInfo kInfo = {"auxsource", "AUX source", 0, nullptr, false, true, 0, nullptr};
    return kInfo;
  }

 private:
  bool gate_ = false;
  bool aux_ = false;
};

// Passes its input through, until its call number goodCalls: from then on it outputs NaN (a
// blown-up filter) until reset(). It blows up only once.
class NanUntilResetEngine final : public VoiceEngine {
 public:
  explicit NanUntilResetEngine(int goodCalls) : goodCalls_(goodCalls) {}
  void prepare(double, int) override {}
  void reset() override { blown_ = false; }
  void noteOn(float, float) override {}
  void noteOff() override {}
  void setNote(float) override {}
  void process(const float* const* in, float* const* out, const ParamValues&, int n) override {
    if (!fired_ && calls_++ == goodCalls_) blown_ = fired_ = true;
    for (int i = 0; i < n; ++i) {
      out[0][i] = blown_ ? std::nanf("") : (in ? in[0][i] : 0.0f);
      out[1][i] = blown_ ? std::nanf("") : (in ? in[1][i] : 0.0f);
    }
  }
  const EngineInfo& info() const override {
    static const EngineInfo kInfo = {"nanuntilreset", "NaN until reset", 0, nullptr, true, false, 0, nullptr};
    return kInfo;
  }

 private:
  int goodCalls_;
  int calls_ = 0;
  bool blown_ = false;
  bool fired_ = false;
};

// Uses input and one side input: out L = its input L (the voice), out R = side 1 (zeros when
// absent). P1 picks the side input's source (SideSource quarters).
class VoiceAndSideEngine final : public VoiceEngine {
 public:
  void prepare(double, int) override {}
  void reset() override {}
  void noteOn(float, float) override {}
  void noteOff() override {}
  void setNote(float) override {}
  void process(const float* const* in, float* const* out, const ParamValues&, int n) override {
    for (int i = 0; i < n; ++i) {
      out[0][i] = in && in[0] ? in[0][i] : 0.0f;
      out[1][i] = in && in[2] ? in[2][i] : 0.0f;
    }
  }
  static SideSource source(int, const ParamValues& p) { return sideSourceOf(p.p[0]); }
  const EngineInfo& info() const override {
    static const EngineInfo kInfo = {"voiceandside", "Voice and side", 0, nullptr, true, false, 1, source};
    return kInfo;
  }
};

}  // namespace eg13::test
