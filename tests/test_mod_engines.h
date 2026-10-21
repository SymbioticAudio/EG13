#pragma once

#include <cstdint>
#include <limits>

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

}  // namespace eg13::test
