#pragma once

#include <memory>

#include "engine/voice_engine.h"

namespace eg13 {

// Adapter for surge-synthesizer/sst-filters (GPL-3.0-or-later), shown as "Classic Filters":
// one of 205 filter types chosen on the card. Runs at the host rate.
// L and R are lanes 0 and 1 of one SIMD filter: the right lane costs nothing extra and, fed the
// same samples while the input is mono, holds exactly the left lane's state. So it always runs
// and setStereo() needs no action: spec §5.1's second instance is that lane.
class SstFilterEngine final : public VoiceEngine {
 public:
  static constexpr int kTypeSteps = 256;  // P3 resolution: type = floor(P3 * 256)

  SstFilterEngine();
  ~SstFilterEngine() override;

  void prepare(double hostRate, int maxBlock) override;
  void reset() override;  // audio-thread safe: no allocation
  void noteOn(float, float) override {}
  void noteOff() override {}
  void setNote(float) override {}
  void process(const float* const* in, float* const* out, const ParamValues& p,
               int n) override;
  const EngineInfo& info() const override;

  static const EngineInfo& staticInfo();
  static int typeCount();
  static int typeIndex(float normalized);  // P3 -> 0..typeCount()-1 (NaN -> 0)
  static float typeValue(int index);       // the centre of a type's P3 step
  static const char* typeName(int index);
  static bool typeHasCharacter(int index);  // the type uses sst's extra coefficient (P4)
  static float cutoffNote(float normalized);  // P1 -> semitones from A440, sst's cutoff unit

 private:
  struct Impl;
  void selectType(int type);

  std::unique_ptr<Impl> impl_;
  double rate_ = 48000.0;
  int type_ = -1;
  int block_ = 0;
  bool snap_ = true;
};

}  // namespace eg13
