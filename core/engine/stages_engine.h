#pragma once

#include <memory>

#include "engine/mod_engine.h"

namespace eg13 {

// Stages' segment generator (stages/segment_generator.h), shown as "Envelope". One group of up
// to six segments with the gate patched into its first segment, as on the module. Runs at the
// module's 31.25 kHz in its 8-sample blocks. One output, 0..1.
//
// P1 Mode: ADSR, AD, Looping, Custom (append-only, kModSelectorSteps). Segment k (0..5) reads
// P(2 + 2k) as its primary (the slider: a Ramp's time, a Hold's or Step's level) and P(3 + 2k) as
// its secondary (the pot: a Ramp's shape, a Hold's time, a Step's portamento), as VCV's Stages
// reads its sliders and pots (AudibleInstruments Stages.cpp:226-227). ADSR plays segments 1-4,
// AD and Looping segments 1-2, Custom the segments of its EnvelopeShape (patch data).
class StagesEngine final : public NativeModEngine {
 public:
  static constexpr double kNativeRate = 31250.0;  // stages/segment_generator.h:43
  static constexpr int kNativeBlock = 8;          // stages/io_buffer.h:40
  enum Mode { Adsr = 0, Ad = 1, Looping = 2, Custom = 3 };
  static constexpr int kNumModes = 4;

  StagesEngine();
  ~StagesEngine() override;

  const ModEngineInfo& info() const override;
  static const ModEngineInfo& staticInfo();
  void setShape(const EnvelopeShape& shape) override { custom_ = sanitized(shape); }

  static int modeIndex(float normalized);  // P1
  // A Ramp segment's duration at this primary value: 1 / (lut_env_frequency[p * 2048] * 31250).
  static float segmentSeconds(float primary);
  // The segments each mode plays; Custom plays `custom`.
  static EnvelopeShape modeShape(int mode, const EnvelopeShape& custom);

 private:
  void resetEngine() override;
  void renderBlock(const ParamValues& p, const uint8_t* gate, const double* beat, double beatsPerNative,
                   bool beatJump, float (*block)[kMaxNativeBlock]) override;

  struct Impl;
  std::unique_ptr<Impl> impl_;
  EnvelopeShape custom_;
  EnvelopeShape configured_;
  bool configure_ = true;  // Configure() before the next block
};

}  // namespace eg13
