#pragma once

#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include "engine/mod_engine.h"

namespace eg13::test {

// A native-rate engine that writes, per native sample, its global index (output 0), its gate
// flags (output 1) and its beat position (output 2), and keeps the last block's inputs.
class CountingModEngine final : public NativeModEngine {
 public:
  CountingModEngine(double rate, int block) : NativeModEngine(rate, block) {}
  const ModEngineInfo& info() const override {
    static const ModOutputInfo kOutputs[3] = {{"Index", false}, {"Gate", false}, {"Beat", false}};
    static const ModEngineInfo kInfo = {{"counting", "Counting", 0, nullptr, false}, 3, kOutputs, false};
    return kInfo;
  }
  int blocks = 0;
  bool lastJump = false;
  uint8_t lastGates[kMaxNativeBlock] = {};
  double lastBeats[kMaxNativeBlock] = {};

 private:
  void resetEngine() override {
    index_ = 0;
    blocks = 0;
    lastJump = false;
  }
  void renderBlock(const ParamValues&, const uint8_t* gate, const double* beat, double, bool jump,
                   float (*block)[kMaxNativeBlock]) override {
    for (int i = 0; i < nativeBlock(); ++i) {
      block[0][i] = float(index_++);
      block[1][i] = float(gate[i]);
      block[2][i] = float(beat[i]);
      lastGates[i] = gate[i];
      lastBeats[i] = beat[i];
    }
    lastJump = jump;
    ++blocks;
  }
  int64_t index_ = 0;
};

// Outputs NaN on output 0 for the first tick after construction (a one-off glitch that survives
// reset()), then 0.25. Counts its resets.
class NanModEngine final : public ModEngine {
 public:
  void prepare(double) override {}
  void reset() override { ++resets; }
  void process(const ModContext&, const ParamValues&, int, float* out) override {
    for (int k = 0; k < kMaxModOutputs; ++k) out[k] = 0.0f;
    out[0] = ticks++ == 0 ? std::numeric_limits<float>::quiet_NaN() : 0.25f;
  }
  const ModEngineInfo& info() const override {
    static const ModOutputInfo kOutputs[1] = {{"Out", false}};
    static const ModEngineInfo kInfo = {{"nan", "NaN", 0, nullptr, false}, 1, kOutputs, false};
    return kInfo;
  }
  int ticks = 0;
  int resets = 0;
};

// Outputs its own P1..P8 (to watch what the matrix gives a slot).
class EchoModEngine final : public ModEngine {
 public:
  void prepare(double) override {}
  void reset() override {}
  void process(const ModContext&, const ParamValues& p, int, float* out) override {
    for (int k = 0; k < kMaxModOutputs; ++k) out[k] = p.p[k];
  }
  const ModEngineInfo& info() const override {
    static const ModOutputInfo kOutputs[8] = {{"P1", false}, {"P2", false}, {"P3", false}, {"P4", false},
                                              {"P5", false}, {"P6", false}, {"P7", false}, {"P8", false}};
    static const ModEngineInfo kInfo = {{"echo", "Echo", 0, nullptr, false}, 8, kOutputs, false};
    return kInfo;
  }
};

// Counts its ticks and reports what it hears: output 0 = 0.01 x ticks since reset, 1 = gate,
// 2 = 0.1 x retriggers since reset, 3 = the tick's beat position, 4 = 0.001 x samples since reset.
class StepModEngine final : public ModEngine {
 public:
  void prepare(double) override {}
  void reset() override { ticks_ = retriggers_ = samples_ = jumps_ = 0; }
  void process(const ModContext& ctx, const ParamValues&, int n, float* out) override {
    ++ticks_;
    if (ctx.retrigger) ++retriggers_;
    if (ctx.beatJump) ++jumps_;
    samples_ += n;
    for (int k = 0; k < kMaxModOutputs; ++k) out[k] = 0.0f;
    out[0] = 0.01f * float(ticks_);
    out[1] = ctx.gate ? 1.0f : 0.0f;
    out[2] = 0.1f * float(retriggers_);
    out[3] = float(ctx.beat);
    out[4] = 0.001f * float(samples_);
    out[5] = float(jumps_);
  }
  const ModEngineInfo& info() const override {
    static const ModOutputInfo kOutputs[6] = {{"Ticks", false}, {"Gate", false}, {"Retriggers", false},
                                              {"Beat", false}, {"Samples", false}, {"Jumps", false}};
    static const ModEngineInfo kInfo = {{"step", "Step", 0, nullptr, false}, 6, kOutputs, false};
    return kInfo;
  }

 private:
  int ticks_ = 0;
  int retriggers_ = 0;
  int samples_ = 0;
  int jumps_ = 0;
};

// Test mod slots: 0 Off, 1 Echo, 2 Step, 3 NaN.
enum TestModEngine : int { kTestOff = 0, kTestEcho = 1, kTestStep = 2, kTestNan = 3 };
inline std::vector<std::unique_ptr<ModEngine>> testModEngines() {
  std::vector<std::unique_ptr<ModEngine>> engines;
  engines.push_back(std::make_unique<OffModEngine>());
  engines.push_back(std::make_unique<EchoModEngine>());
  engines.push_back(std::make_unique<StepModEngine>());
  engines.push_back(std::make_unique<NanModEngine>());
  return engines;
}

}  // namespace eg13::test
