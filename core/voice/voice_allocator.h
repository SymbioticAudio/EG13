#pragma once

#include "voice/mono_note_stack.h"

namespace eg13 {

enum class VoiceMode { Poly = 0, Mono = 1, Legato = 2 };

struct VoiceAction {
  enum class Type { Trigger, Glide, Release };
  Type type;
  int voice;
  int key;
  float velocity;
};

struct VoiceActions {
  static constexpr int kMax = 8;
  VoiceAction items[kMax] = {};
  int count = 0;
  void add(VoiceAction action) {
    if (count < kMax) items[count++] = action;
  }
};

// Decides which voice plays each note. Pure logic: no audio, no allocation.
class VoiceAllocator {
 public:
  static constexpr int kMaxVoices = 8;

  VoiceActions configure(VoiceMode mode, int voiceCount);
  VoiceActions noteOn(int key, float velocity);
  VoiceActions noteOff(int key);
  VoiceActions setSustain(bool down);
  VoiceActions allNotesOff();
  void voiceIdle(int voice);  // the voice went silent after its release

  VoiceMode mode() const { return mode_; }
  int voiceCount() const { return voiceCount_; }
  bool sounding(int voice) const { return slots_[voice].sounding; }
  int soundingCount() const;

 private:
  struct Slot {
    int key = -1;
    bool held = false;
    bool sustained = false;
    bool sounding = false;
    unsigned startOrder = 0;
    unsigned releaseOrder = 0;
  };

  int pickPolyVoice(int key) const;
  VoiceAction release(int voice);

  VoiceMode mode_ = VoiceMode::Poly;
  int voiceCount_ = kMaxVoices;
  bool sustain_ = false;
  unsigned clock_ = 0;
  Slot slots_[kMaxVoices];
  MonoNoteStack stack_;
};

}  // namespace eg13
