#pragma once

#include <memory>
#include <vector>

#include "util/smoothed_value.h"
#include "voice/mono_note_stack.h"
#include "voice/source_factory.h"
#include "voice/voice.h"
#include "voice/voice_allocator.h"

namespace eg13 {

struct PartSettings {
  float level = 1.0f;
  float pan = 0.0f;
  float glide = 0.0f;  // normalized; see glideSeconds()
  int transpose = 0;
  int baseNote = 60;
  int midiChannel = 0;  // 0 = Omni, 1..16
  int keyLow = 0;
  int keyHigh = 127;
  VoiceMode voiceMode = VoiceMode::Poly;
  int voiceCount = 8;
  bool alwaysOn = false;
  int bendRange = 2;
  bool mono = false;
  int output = 0;  // 0 = main, 1 = Bus 2, 2 = other part only
  int engine = 0;  // SourceEngine
  int inputSource = 0;  // 0 external, 1 other part, 2 both (routed by the Synth)
};

// One part: 8 voices, their allocator and the part's settings.
class Part {
 public:
  static constexpr int kVoices = VoiceAllocator::kMaxVoices;

  Part();                                // every source engine
  explicit Part(SourceFactory factory);  // one engine per voice (tests)

  void prepare(double hostRate, int maxBlock,
               Voice::Alignment alignment = Voice::Alignment::Notes);  // main thread
  void reset();
  void applySettings(const PartSettings& settings);  // once per block
  const PartSettings& settings() const { return settings_; }

  bool acceptsChannel(int channel) const;  // 0..15, negative = any
  bool acceptsKey(int key) const;

  void noteOn(int key, float velocity);
  void noteOff(int key);
  void pitchBend(float value);  // -1..1
  void sustain(bool down);
  void allNotesOff();

  // Overwrites out[0], out[1] with n samples (n <= maxBlock). in: nullable.
  void process(const float* const* in, float* const* out, const ParamValues& source, int n);
  void process(float* const* out, const ParamValues& source, int n) {
    process(nullptr, out, source, n);
  }

  int activeVoiceCount() const;
  int latencySamples() const { return voices_[0]->latencySamples(); }

 private:
  void perform(const VoiceActions& actions);
  void updatePitchOffset();
  void updateGains();

  std::vector<std::unique_ptr<Voice>> voices_;
  VoiceAllocator allocator_;
  PartSettings settings_;
  MonoNoteStack alwaysOnNotes_;
  bool alwaysOn_ = false;
  float bend_ = 0.0f;
  SmoothedValue gainL_;
  SmoothedValue gainR_;
  std::vector<float> mix_[2];
};

}  // namespace eg13
