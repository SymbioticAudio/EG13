#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "engine/param_format.h"
#include "engine/voice_engine.h"

namespace eg13 {

constexpr int kMaxModOutputs = 8;   // Marbles has seven (T1-T3, X1-X3, Y)
constexpr int kModParamCount = 16;  // P1..P16 of a mod slot (spec §7.1)

// Stepped mod-engine P's are append-only option lists read in kModSelectorSteps fixed steps, as
// the Ring Mod / Vocoder's selectors are (warps_engine.h): adding an option never changes what a
// saved value means. Steps past the last option read as the last option. RULE: when an option is
// appended, that version's state migration snaps every saved value above the old last option's
// step back to modOptionValue(old last), and a host default that relied on "past the end reads
// as the last option" is changed to modOptionValue(old last).
constexpr int kModSelectorSteps = 16;
inline int modOption(float normalized, int options) {
  return std::min(stepIndex(normalized, kModSelectorSteps), options - 1);
}
inline float modOptionValue(int option) { return (float(option) + 0.5f) / float(kModSelectorSteps); }
// The name of a stepped P's option: names[modOption(v, count)]. The option formatters of the mod
// engines call it.
inline void formatOption(const char* const* names, int count, float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%s", names[modOption(v, count)]);
}

struct ModOutputInfo {
  const char* name;
  bool bipolar;  // -1..1; otherwise 0..1
};

// A mod engine's description: its parameters as an EngineInfo, so slot code names and formats
// them like any other slot's, plus its outputs.
struct ModEngineInfo {
  EngineInfo engine;
  int outputCount;
  const ModOutputInfo* outputs;
  bool perPartByDefault;  // the scope a fresh slot gives this engine (spec §4: Random per part)
};

// Patch data of the Envelope's Custom mode (spec §7.1: multi-segment shapes are patch data): the
// type and loop flag of each of up to six segments, as the module's buttons set them.
struct EnvelopeShape {
  static constexpr int kMaxSegments = 6;
  enum Type : uint8_t { Ramp = 0, Step = 1, Hold = 2 };
  uint8_t count = 4;  // the default is an ADSR: Ramp, Ramp, Hold (looping), Ramp
  uint8_t type[kMaxSegments] = {Ramp, Ramp, Hold, Ramp, Ramp, Ramp};
  uint8_t loop[kMaxSegments] = {0, 0, 1, 0, 0, 0};
};
bool operator==(const EnvelopeShape& a, const EnvelopeShape& b);
inline bool operator!=(const EnvelopeShape& a, const EnvelopeShape& b) { return !(a == b); }
// count 1..6; unknown types read as Ramp; loop flags 0/1; entries past count are kept (sanitized).
EnvelopeShape sanitized(const EnvelopeShape& shape);

// Gate flag bits, as stmlib/utils/gate_flags.h and peaks/gate_processor.h define them (each
// adapter checks this with a static_assert).
enum : uint8_t { kGateLow = 0, kGateHigh = 1, kGateRising = 2, kGateFalling = 4 };

// What a mod engine hears during one control tick.
struct ModContext {
  bool gate = false;       // high while the note (per-voice) or the part's keys (per-part) are held
  bool retrigger = false;  // a note started since the last tick: an edge even if the gate was high
  double beat = 0.0;       // beat position (quarter notes) at the start of the tick
  double beatsPerSample = 0.0;  // at the host rate
  bool beatJump = false;   // the transport started or jumped: tempo-synced engines lock to it
};

// A modulation source (spec §3.1): up to kMaxModOutputs control outputs.
class ModEngine {
 public:
  virtual ~ModEngine() = default;
  virtual void prepare(double hostRate) = 0;  // main thread; the only place that may allocate
  virtual void reset() = 0;                   // audio-thread safe
  // Advances n host samples and writes every output's value at the end of them into
  // out[0..kMaxModOutputs) (0 beyond the engine's outputCount).
  virtual void process(const ModContext& ctx, const ParamValues& p, int n, float* out) = 0;
  virtual const ModEngineInfo& info() const = 0;
  virtual void setShape(const EnvelopeShape&) {}  // Envelope's Custom mode; others ignore it
  virtual void setSeed(uint32_t) {}               // engines with their own random generator
};

// Runs a firmware module at its native rate and block size. Whole native blocks are rendered as
// host time reaches them (up to one block ahead); each process() reports the newest native
// sample whose time has come. Gate changes take effect at the first native sample of the next
// rendered block. At host rate = native rate with n a multiple of the block it is the bare
// module, sample for sample.
// Native time is computed exactly from the host sample count, as the fraction
// hostSamples * nativeRate / hostRate in integers (rates rounded to 1 mHz), never accumulated,
// so it cannot drift however long the session runs.
class NativeModEngine : public ModEngine {
 public:
  static constexpr int kMaxNativeBlock = 8;

  NativeModEngine(double nativeRate, int nativeBlock);
  // Sets the host rate and restarts the native-time grid (as reset() does, but the engine's own
  // state and the held outputs are kept).
  void prepare(double hostRate) final;
  void reset() final;
  void process(const ModContext& ctx, const ParamValues& p, int n, float* out) final;

  double nativeRate() const { return nativeRate_; }
  int nativeBlock() const { return nativeBlock_; }
  int64_t renderedSamples() const { return rendered_; }  // tests

 protected:
  virtual void resetEngine() = 0;
  // One native block: gate[i] gate flags, beat[i] beat position of native sample i. Writes
  // block[k][i] for every output k < info().outputCount.
  virtual void renderBlock(const ParamValues& p, const uint8_t* gate, const double* beat, double beatsPerNative,
                           bool beatJump, float (*block)[kMaxNativeBlock]) = 0;

 private:
  double nativeRate_;
  int nativeBlock_;
  double ratio_ = 1.0;     // native samples per host sample
  // Native time = periodNative_ + hostPhase_ * rateNum_ / rateDen_, with rateNum_ / rateDen_ the
  // reduced ratio of the rates in mHz: hostPhase_ host samples into the current period of
  // rateDen_ host samples (exactly rateNum_ native samples), so no product can overflow.
  int64_t rateNum_ = 1;
  int64_t rateDen_ = 1;
  int64_t periodNative_ = 0;  // native samples of the whole periods since reset
  int64_t hostPhase_ = 0;     // 0 .. rateDen_ - 1 between calls
  int64_t rendered_ = 0;   // native samples rendered
  bool high_ = false;      // the gate at the last rendered native sample
  bool retrigger_ = false;
  bool fall_ = false;      // a retrigger with the gate already low: fall on the next sample
  bool jump_ = false;
  float block_[kMaxModOutputs][kMaxNativeBlock] = {};
  float held_[kMaxModOutputs] = {};
};

// The empty mod slot.
class OffModEngine final : public ModEngine {
 public:
  static const ModEngineInfo& staticInfo();
  void prepare(double) override {}
  void reset() override {}
  void process(const ModContext&, const ParamValues&, int, float* out) override {
    std::fill(out, out + kMaxModOutputs, 0.0f);
  }
  const ModEngineInfo& info() const override { return staticInfo(); }
};

}  // namespace eg13
