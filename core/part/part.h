#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "engine/pot_smoother.h"
#include "mod/beat_clock.h"
#include "mod/part_modulation.h"
#include "util/smoothed_value.h"
#include "voice/mono_note_stack.h"
#include "voice/source_factory.h"
#include "voice/voice.h"
#include "voice/voice_allocator.h"

namespace eg13 {

// MIDI's release velocity when a note-off carries none (a note-on with velocity 0): 64.
constexpr float kDefaultReleaseVelocity = 64.0f / 127.0f;

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
  int filterEngine = 0;  // FilterEngine
  int shaperEngine = 0;  // ShaperEngine
  int inputSource = 0;  // 0 external, 1 other part, 2 both (routed by the Synth)
  int modEngine[kNumModSlots] = {};      // ModEngineKind per mod slot
  bool modPerPart[kNumModSlots] = {};    // scope per mod slot
  int mpe = 0;              // 0 off, 1 lower zone, 2 upper zone (the Synth works out the channels)
  int mpeBendRange = 48;    // semitones of a member channel's full pitch bend
  int mpeMaster = 0;        // MPE: the zone's master channel (0..15)
  int mpeFirst = 1;         // MPE: the zone's member channels, first..last (0..15)
  int mpeLast = 15;
};

// Per-note expression (MPE member channels, polyphonic aftertouch, CLAP note expressions).
enum class Expression { Bend, Pressure, Slide };  // Bend in semitones; Pressure and Slide 0..1
// Part-wide controllers the matrix reads.
enum class PartController { ModWheel, Slide, ChannelPressure, CcA, CcB, CcC, CcD };

// Where the part is in musical time during a process() call (the Synth's beat clock). The beat
// at sample i of the call is beatAt(i): counted in whole samples from the clock's origin, so each
// tick's beat is the same whatever the host's block sizes (BeatClock).
struct ModTiming {
  double origin = 0.0;   // the clock's origin beat
  int64_t elapsed = 0;   // samples from the origin to the start of the call
  double beatsPerSample = 2.0 / 48000.0;  // 120 BPM at 48 kHz
  bool jump = false;  // the transport started or jumped
  double barStart = 0.0;  // the beat where a bar starts
  double beatsPerBar = 4.0;
  double beatAt(int offset) const { return origin + double(elapsed + offset) * beatsPerSample; }
};

// Everything one process() call of a modulated part reads besides audio.
struct PartParams {
  SlotParams slots;     // host values of the Source, Filter and Shaper P's
  ModParams mod;        // mod slot P's, routes and amounts, envelope shapes
  ModTiming timing;
  float voiceGain = 1.0f;  // the master Voice Gain (spec §5.2's Amp gain)
};

// One part: 8 voices, their allocator, the part's modulation and the part's settings.
class Part {
 public:
  static_assert(VoiceAllocator::kMaxVoices == PartModulation::kVoices,
                "the Part indexes its modulation by allocator voice");
  static constexpr int kVoices = VoiceAllocator::kMaxVoices;
  static constexpr int kChannels = 16;

  using SlotFactory = std::vector<SourceSpec> (*)();

  explicit Part(ModEngineFactory mods = makeAllModEngines);  // every engine of every slot
  explicit Part(SourceFactory factory, ModEngineFactory mods = makeAllModEngines);  // tests
  Part(SlotFactory sources, SlotFactory filters, SlotFactory shapers,
       ModEngineFactory mods = makeAllModEngines);  // per voice (tests)

  void setSeedBase(uint32_t base);  // main thread, before prepare(): the part's random seeds
  void prepare(double hostRate, int maxBlock, Voice::Alignment alignment = Voice::Alignment::Notes,
               ConverterQuality q = ConverterQuality::Studio);  // main thread
  void reset();
  void applySettings(const PartSettings& settings);  // once per block
  const PartSettings& settings() const { return settings_; }

  bool acceptsChannel(int channel) const;  // 0..15, negative = any
  bool acceptsKey(int key) const;
  bool isMemberChannel(int channel) const;  // an MPE member channel of this part's zone

  void noteOn(int key, float velocity, int channel = 0);
  void noteOff(int key, float releaseVelocity = kDefaultReleaseVelocity);
  void pitchBend(float value);  // -1..1
  void sustain(bool down);
  void allNotesOff();
  void setController(PartController controller, float value);  // 0..1 (ModWheel..CcD)
  // An MPE member channel's bend (semitones), pressure or slide: every voice on that channel,
  // and the next notes on it.
  void memberExpression(int channel, Expression expression, float value);
  // One note's expression: the voices playing `key` (negative: any key) on `channel` (negative:
  // any channel).
  void noteExpression(int channel, int key, Expression expression, float value);

  // Modulated (M4): the part's mod slots and matrix on a fixed 16-sample grid, then each voice's
  // Amp and pan. Overwrites out[0], out[1] with n samples (n <= maxBlock).
  void process(const VoiceInputs& in, float* const* out, const PartParams& params, int n);
  // Unmodulated (M1-M3 tests): no mod slots, Amp, pan or gains.
  void process(const VoiceInputs& in, float* const* out, const SlotParams& params, int n);
  // Source parameters only; in: the part's Input (nullable).
  void process(const float* const* in, float* const* out, const ParamValues& source, int n);
  void process(float* const* out, const ParamValues& source, int n) {
    process(nullptr, out, source, n);
  }

  int activeVoiceCount() const;
  int latencySamples() const { return voices_[0]->latencySamples(); }
  const VoiceControl& lastControl(int voice) const { return controls_[voice]; }  // tests
  const PartModulation& modulation() const { return modulation_; }               // tests

 private:
  void perform(const VoiceActions& actions);
  void startNote(int v, int key, float velocity);  // a voice's new note: its mod sources
  void moveNote(int v, int key);  // the voice now plays `key` (its channel and expression), no retrigger
  void delayInputSide(const float* const* input, int n);  // fills inputSide_[0..n)
  void updatePitchOffset();
  void updateGains(float level);
  // mix_ x the part's gains (and Mono) into out. modulated: the Part Level comes from the
  // matrix, delayed by the voices' latency so it lines up with their sound.
  void writeOutput(float* const* out, int from, int n, bool modulated);
  ModVoiceInputs voiceInputs(int v) const;
  ModPartInputs partInputs(const PartParams& params, int offset) const;
  void startTick(const PartParams& params, int offset);  // the grid: part slots, then every voice
  void controlVoice(int v, const PartParams& params, const ModPartInputs& part, int length);

  std::vector<std::unique_ptr<Voice>> voices_;
  VoiceAllocator allocator_;
  PartSettings settings_;
  MonoNoteStack alwaysOnNotes_;
  bool alwaysOn_ = false;
  float bend_ = 0.0f;
  SmoothedValue gainL_;
  SmoothedValue gainR_;
  std::vector<float> mix_[2];
  // The Input for the Shaper's side signals: mono, delayed by the voices' Source latency so
  // it reaches the Shaper with the voice (one line shared by every voice).
  std::vector<float> inputDelay_;
  int inputDelayPos_ = 0;
  std::vector<float> inputSide_;

  // Modulation (M4).
  PartModulation modulation_;
  VoiceControl controls_[kVoices];
  bool controlValid_[kVoices] = {};  // controls_[v] belongs to the current tick
  int tickPos_ = 0;                  // samples into the 16-sample grid tick
  bool pendingJump_ = false;
  bool modulated_ = false;   // the last process() call was the modulated path
  float partLevel_ = 1.0f;   // its Part Level (base + routes) at the last tick
  float outputLevel_ = 1.0f;  // the Part Level applied now: partLevel_, latencySamples() later
  std::vector<float> levelLine_;  // partLevel_ per sample, latencySamples() long
  int levelLinePos_ = 0;
  // Per-voice sources.
  int voiceKey_[kVoices] = {};
  int voiceChannel_[kVoices] = {};
  float voiceVelocity_[kVoices] = {};
  float voiceRelease_[kVoices] = {};
  float voiceRandom_[kVoices] = {};
  float voicePressure_[kVoices] = {};
  float voiceSlide_[kVoices] = {};
  float voiceBend_[kVoices] = {};
  bool voiceRetrigger_[kVoices] = {};
  float keyRelease_[128] = {};
  int keyChannel_[128] = {};  // the channel each key was last played on (returns to held keys)
  int lastVoice_ = -1;
  // MPE member channels' state, for notes that start after it was sent.
  float channelBend_[kChannels] = {};
  float channelPressure_[kChannels] = {};
  float channelSlide_[kChannels] = {};
  // Part-wide controllers and gates.
  float modWheel_ = 0.0f;
  float pressure_ = 0.0f;
  float slide_ = 0.0f;
  float cc_[4] = {};
  bool partGate_ = false;
  bool partRetrigger_ = false;
  uint32_t seed_ = 0x13579BDFu;  // the Random source's seed (setSeedBase)
  uint32_t random_ = 0x13579BDFu;
  // Audio Input chord scaling (ruling 26): 1 / open Audio Input voices, glided over
  // kMixDeclickSeconds so a note on or off does not step the other voices. A voice with an open
  // gate follows it; a released voice keeps the scale it had at note-off until it frees.
  Declicker inputScaleSmoother_{kMixDeclickSeconds};
  float inputScale_ = 1.0f;                 // the smoothed scale at the end of the last tick
  float voiceInputScale_[kVoices] = {};     // each voice's scale; frozen once its gate closes
};

}  // namespace eg13
