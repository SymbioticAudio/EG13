#include "voice/voice_allocator.h"

namespace eg13 {

VoiceActions VoiceAllocator::configure(VoiceMode mode, int voiceCount) {
  if (voiceCount < 1) voiceCount = 1;
  if (voiceCount > kMaxVoices) voiceCount = kMaxVoices;
  if (mode == mode_ && voiceCount == voiceCount_) return {};
  VoiceActions actions = allNotesOff();
  mode_ = mode;
  voiceCount_ = voiceCount;
  return actions;
}

VoiceActions VoiceAllocator::noteOn(int key, float velocity) {
  VoiceActions actions;
  if (mode_ != VoiceMode::Poly) {
    const bool wasEmpty = stack_.empty();
    stack_.noteOn(key, velocity);
    Slot& s = slots_[0];
    s.key = key;
    s.held = true;
    s.sustained = false;
    s.sounding = true;
    if (mode_ == VoiceMode::Legato && !wasEmpty) {
      actions.add({VoiceAction::Type::Glide, 0, key, velocity});
    } else {
      s.startOrder = ++clock_;
      actions.add({VoiceAction::Type::Trigger, 0, key, velocity});
    }
    return actions;
  }
  const int v = pickPolyVoice(key);
  Slot& s = slots_[v];
  s.key = key;
  s.held = true;
  s.sustained = false;
  s.sounding = true;
  s.startOrder = ++clock_;
  actions.add({VoiceAction::Type::Trigger, v, key, velocity});
  return actions;
}

VoiceActions VoiceAllocator::noteOff(int key) {
  VoiceActions actions;
  if (mode_ != VoiceMode::Poly) {
    const bool wasTop = !stack_.empty() && stack_.topKey() == key;
    stack_.noteOff(key);
    Slot& s = slots_[0];
    if (stack_.empty()) {
      if (!s.held) return actions;
      s.held = false;
      if (sustain_) {
        s.sustained = true;
      } else {
        actions.add(release(0));
      }
    } else if (wasTop) {
      s.key = stack_.topKey();
      if (mode_ == VoiceMode::Legato) {
        actions.add({VoiceAction::Type::Glide, 0, s.key, stack_.topVelocity()});
      } else {
        s.startOrder = ++clock_;
        actions.add({VoiceAction::Type::Trigger, 0, s.key, stack_.topVelocity()});
      }
    }
    return actions;
  }
  for (int v = 0; v < kMaxVoices; ++v) {
    Slot& s = slots_[v];
    if (s.held && s.key == key) {
      s.held = false;
      if (sustain_) {
        s.sustained = true;
      } else {
        actions.add(release(v));
      }
    }
  }
  return actions;
}

VoiceActions VoiceAllocator::setSustain(bool down) {
  VoiceActions actions;
  sustain_ = down;
  if (!down) {
    for (int v = 0; v < kMaxVoices; ++v) {
      if (slots_[v].sustained) actions.add(release(v));
    }
  }
  return actions;
}

VoiceActions VoiceAllocator::allNotesOff() {
  VoiceActions actions;
  stack_.clear();
  sustain_ = false;
  for (int v = 0; v < kMaxVoices; ++v) {
    if (slots_[v].held || slots_[v].sustained) actions.add(release(v));
  }
  return actions;
}

void VoiceAllocator::voiceIdle(int voice) {
  Slot& s = slots_[voice];
  if (s.held || s.sustained) return;
  s.sounding = false;
  s.key = -1;
}

int VoiceAllocator::soundingCount() const {
  int count = 0;
  for (const Slot& s : slots_) count += s.sounding ? 1 : 0;
  return count;
}

int VoiceAllocator::pickPolyVoice(int key) const {
  for (int v = 0; v < voiceCount_; ++v) {
    if (slots_[v].sounding && slots_[v].key == key) return v;
  }
  for (int v = 0; v < voiceCount_; ++v) {
    if (!slots_[v].sounding) return v;
  }
  int best = -1;
  for (int v = 0; v < voiceCount_; ++v) {
    const Slot& s = slots_[v];
    if (s.held || s.sustained) continue;
    if (best < 0 || s.releaseOrder < slots_[best].releaseOrder) best = v;
  }
  if (best >= 0) return best;
  best = 0;
  for (int v = 1; v < voiceCount_; ++v) {
    if (slots_[v].startOrder < slots_[best].startOrder) best = v;
  }
  return best;
}

VoiceAction VoiceAllocator::release(int voice) {
  Slot& s = slots_[voice];
  s.held = false;
  s.sustained = false;
  s.releaseOrder = ++clock_;
  return {VoiceAction::Type::Release, voice, s.key, 0.0f};
}

}  // namespace eg13
