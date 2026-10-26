#pragma once

#include <cstdint>
#include <vector>

#include "mod/mod_factory.h"
#include "mod/mod_routes.h"
#include "mod/mod_slot.h"
#include "mod/voice_control.h"

namespace eg13 {

// The host values a part's modulation reads in one call: every mod slot's P1..P16, the routes and
// their amounts, and each slot's Custom envelope shape (patch data).
struct ModParams {
  ParamValues slot[kNumModSlots];
  RouteSet routes;
  EnvelopeShape shapes[kNumModSlots];
};

// One voice's fixed sources for a tick (the Part keeps them).
struct ModVoiceInputs {
  bool active = false;     // for the Part: the voice is sounding (it calls processVoice() for it)
  bool gate = false;       // the per-voice mod engines' gate
  bool retrigger = false;  // a note started on the voice since the last tick
  float note = 60.0f;      // the voice's key, glided
  float velocity = 0.0f;
  float releaseVelocity = 0.0f;
  float random = 0.0f;
  float pressure = 0.0f;
  float slide = 0.0f;
  float bend = 0.0f;  // per-note bend, semitones
};

// The part's fixed sources and timing for a tick.
struct ModPartInputs {
  float modWheel = 0.0f;
  float pitchBend = 0.0f;  // -1..1
  float channelPressure = 0.0f;
  float slide = 0.0f;      // part-wide CC74 (provisional ruling 35)
  float cc[4] = {};
  float transportPhase = 0.0f;
  float noteBendRange = 48.0f;  // semitones that read as 100 % Note Bend
  bool gate = false;       // the per-part mod engines' gate: any key held
  bool retrigger = false;  // the first key went down since the last tick
  double beat = 0.0;
  double beatsPerSample = 0.0;
  bool beatJump = false;
  int lastVoice = -1;      // the most recently started voice, -1 or 0..kVoices-1 (provisional ruling 6)
};

// A part's mod slots and matrix (spec §6): 4 part-scope slot instances and 4 per voice, each
// holding every mod engine. Per tick the Part calls processPart() once, then processVoice() for
// each sounding voice.
//
// Order and feedback (provisional ruling 5): routes into a mod slot's P's read every mod output
// as it was at the end of the previous tick; routes into the voice's controls read this tick's.
// So a loop between slots (or a slot into itself) is a one-tick delay, bounded by the 0..1 clamp,
// and the result does not depend on slot order.
class PartModulation {
 public:
  static constexpr int kVoices = 8;

  explicit PartModulation(ModEngineFactory factory = makeAllModEngines);

  void setSeedBase(uint32_t base);  // main thread, before prepare(): every instance its own seed
  void prepare(double hostRate);    // main thread
  void reset();
  // Audio thread: slot `slot` plays `engine`, per part or per voice. A new engine starts from
  // reset; the instances a scope change starts using start from reset too. Call it before
  // processPart() for the tick, never between processPart() and processVoice().
  void configure(int slot, int engine, bool perPart);
  int engine(int slot) const { return engine_[slot]; }
  bool perPart(int slot) const { return perPart_[slot]; }
  void startVoice(int v);  // the voice starts from silence: its per-voice slots restart

  void processPart(const ModParams& params, const ModPartInputs& part, const ModVoiceInputs* voices, int n);
  void processVoice(int v, const ModParams& params, const ModPartInputs& part, const ModVoiceInputs& voice,
                    const SlotParams& base, VoiceControl* control, int n);
  // base + the routes into Part Level (after processPart()), clamped. Per-part slots read this
  // tick's outputs; per-voice slots (of lastVoice) the previous tick's, as they have not run yet.
  // Both read the Part-kind route sums cached by the last processPart() (computed once per tick).
  float partLevel(float base) const;
  // The part's Part FX P1..P16: each base P plus the routes into it, clamped. Same sources and
  // timing as partLevel(). `out` may be the same object as `base`.
  void partFxParams(const ParamValues& base, ParamValues* out) const;

  const float* outputs(int slot, int voice) const;  // voice < 0: the part instance
  // The mod slot's P1..P16 after the routes into them, as of the last tick that ran the
  // instance (voice < 0: the part instance). For the editor's display (Part::modDisplay).
  const ParamValues& modulatedParams(int slot, int voice) const {
    return voice < 0 ? partModulated_[slot] : voiceModulated_[size_t(voice * kNumModSlots + slot)];
  }

 private:
  ModSlot& voiceSlot(int v, int slot) { return voiceSlots_[size_t(v * kNumModSlots + slot)]; }
  const ModSlot& voiceSlot(int v, int slot) const { return voiceSlots_[size_t(v * kNumModSlots + slot)]; }
  void fillFixed(float* source, const ModPartInputs& part, const ModVoiceInputs* voice) const;
  // Mod outputs: per-voice slots from `voice` (none: 0); part slots this tick's or the previous.
  void fillMods(float* source, int voice, bool partCurrent) const;

  std::vector<ModSlot> voiceSlots_;  // [voice * kNumModSlots + slot]
  std::vector<ModSlot> partSlots_;
  int engine_[kNumModSlots] = {};
  bool perPart_[kNumModSlots] = {};
  float partPrevious_[kNumModSlots][kMaxModOutputs] = {};
  float partSource_[kNumSources] = {};
  RouteSums partSums_ = {};  // Part-kind sums for this tick
  ParamValues partModulated_[kNumModSlots];                      // modulatedSlot() of the last run
  ParamValues voiceModulated_[size_t(kVoices * kNumModSlots)];  // same, per voice instance
};

}  // namespace eg13
