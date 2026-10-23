#pragma once

#include <memory>

#include "engine/voice_engine.h"

namespace beads {
class Processor;
}

namespace eg13 {

// Part FX "Granular Delay": Beads (beads::Processor) as its firmware drives it (beads/beads.cc:
// 107-271), at its Quality's codec rate in 4000 blocks per second (M5 plan ruling 15): Digital and
// Clean tape at 48 kHz, Vintage digital at 32 kHz, Dirty tape at 24 kHz. The slot runs this engine
// at the host rate; inside, three NativeRateWrappers (24, 32, 48 kHz) drive one shared processor,
// each padded to the slowest so the latency never depends on the Quality. P1 Dry/Wet, P2 Time,
// P3 Size, P4 Pitch, P5 Density, P6 Shape, P7 Feedback, P8 Reverb, P9 Freeze, P10 Quality, P11 Seed,
// P12 unused, P13-P16 Time / Size / Shape / Pitch Random (the attenuverters; ruling 8). Pots pass
// the firmware's laws and its low-pass, 0.05 per 4 kHz block (beads/cv_reader.h:108-131,
// cv_reader.cc:50-54). With Seed = Notes each note-on is a 1 ms SEED gate (rulings 13, 14).
class BeadsEngine final : public VoiceEngine {
 public:
  // Quality: EG13's order (append-only; P10 in 16 fixed steps). Not Beads' Media order: map with
  // mediaFor(), never compare the two.
  enum Quality { Digital = 0, VintageDigital = 1, CleanTape = 2, DirtyTape = 3 };
  static constexpr int kNumQualities = 4;
  // Seed: Free (SEED unpatched) or Notes (note-ons into SEED). Append-only; P11 in 16 fixed steps.
  enum Seed { SeedFree = 0, SeedNotes = 1 };
  static constexpr int kNumSeeds = 2;
  static constexpr int kBlockRate = 4000;  // beads.cc:50,277

  BeadsEngine();
  ~BeadsEngine() override;

  void setConverterQuality(ConverterQuality q) override;  // reaches the three inner wrappers
  void prepare(double hostRate, int maxBlock) override;
  void reset() override;
  void noteOn(float note, float velocity) override;
  void noteOff() override {}
  void setNote(float) override {}
  void process(const float* const* in, float* const* out, const ParamValues& p, int n) override;
  const EngineInfo& info() const override;
  // The slowest inner path's input-path latency, in host samples (every path is padded to it).
  int nativeLatency() const override;

  // The Quality running now (P10 once a change has faded through).
  Quality quality() const;

  static const EngineInfo& staticInfo();
  static int mediaFor(Quality q);  // a beads::Processor::Media value
  static double rateFor(Quality q);  // 48000, 32000, 48000, 24000
  static int qualityIndex(float p);  // P10 in 16 fixed steps
  static int seedIndex(float p);     // P11 in 16 fixed steps
  // The DETUNE pot law (cv_reader.h:114-129): semitones, +-24, with plateaus at the intervals.
  static float pitchSemitones(float p);
  // The ATTENUVERTER pot law (cv_reader.h:110-112): -1..+1 with a small dead zone at 50 %.
  static float attenuverter(float p);
  // The input stage (ruling 16): unity gain, clamped to +-1 as the manual-gain path does; Dirty
  // tape takes that path's soft clipping at amount 1 (beads/gain_control.cc:149-166). NaN reads as 0.
  static float inputStage(float x, Quality q);

 private:
  struct Impl;
  class Core;
  void applyQuality(int q);

  std::unique_ptr<Impl> impl_;
};

}  // namespace eg13
