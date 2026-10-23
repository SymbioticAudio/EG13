#include "part/part.h"

#include <algorithm>
#include <cmath>
#include <iterator>

#include "engine/engine_registry.h"
#include "synth/param_table.h"
#include "voice/processor_factory.h"

namespace eg13 {

namespace {
constexpr double kGainSmoothingSeconds = 0.01;
}  // namespace

Part::Part(ModEngineFactory mods) : Part(makeAllSources, makeAllFilters, makeAllShapers, mods) {}

Part::Part(SlotFactory sources, SlotFactory filters, SlotFactory shapers, ModEngineFactory mods)
    : modulation_(mods) {
  for (int v = 0; v < kVoices; ++v) voices_.push_back(std::make_unique<Voice>(sources(), filters(), shapers()));
}

Part::Part(SourceFactory factory, ModEngineFactory mods) : modulation_(mods) {
  for (int v = 0; v < kVoices; ++v) voices_.push_back(std::make_unique<Voice>(factory()));
}

void Part::setSeedBase(uint32_t base) {
  modulation_.setSeedBase(base);
  seed_ = 0x13579BDFu ^ (base * 2654435761u);
  if (seed_ == 0) seed_ = 1;
  random_ = seed_;
}

void Part::prepare(double hostRate, int maxBlock, Voice::Alignment alignment) {
  for (auto& voice : voices_) voice->prepare(hostRate, maxBlock, alignment);
  for (auto& buffer : mix_) buffer.assign(size_t(maxBlock), 0.0f);
  int sourceLatency = 0;
  for (auto& voice : voices_) sourceLatency = std::max(sourceLatency, voice->sourceLatencySamples());
  inputDelay_.assign(size_t(sourceLatency), 0.0f);
  inputSide_.assign(size_t(maxBlock), 0.0f);
  levelLine_.assign(size_t(latencySamples()), 1.0f);
  gainL_.prepare(hostRate, kGainSmoothingSeconds);
  gainR_.prepare(hostRate, kGainSmoothingSeconds);
  modulation_.prepare(hostRate);
  inputScaleSmoother_.prepare(hostRate);
  reset();
}

void Part::reset() {
  for (auto& voice : voices_) voice->reset();
  std::fill(inputDelay_.begin(), inputDelay_.end(), 0.0f);
  inputDelayPos_ = 0;
  allocator_.allNotesOff();
  for (int v = 0; v < kVoices; ++v) allocator_.voiceIdle(v);
  alwaysOnNotes_.clear();
  alwaysOn_ = false;
  bend_ = 0.0f;
  modulation_.reset();
  for (auto& c : controls_) c = VoiceControl{};
  inputScaleSmoother_.reset();
  inputScale_ = 1.0f;
  std::fill(std::begin(voiceInputScale_), std::end(voiceInputScale_), 1.0f);
  std::fill(std::begin(controlValid_), std::end(controlValid_), false);
  std::fill(std::begin(voiceRetrigger_), std::end(voiceRetrigger_), false);
  std::fill(std::begin(voicePressure_), std::end(voicePressure_), 0.0f);
  std::fill(std::begin(voiceSlide_), std::end(voiceSlide_), 0.0f);
  std::fill(std::begin(voiceBend_), std::end(voiceBend_), 0.0f);
  std::fill(std::begin(channelBend_), std::end(channelBend_), 0.0f);
  std::fill(std::begin(channelPressure_), std::end(channelPressure_), 0.0f);
  std::fill(std::begin(channelSlide_), std::end(channelSlide_), 0.0f);
  tickPos_ = 0;
  pendingJump_ = false;
  lastVoice_ = -1;
  partGate_ = false;
  partRetrigger_ = false;
  modulated_ = false;
  random_ = seed_;  // the Random source repeats its sequence after a reset, as the mod slots do
  std::fill(std::begin(keyChannel_), std::end(keyChannel_), 0);
  const PartSettings current = settings_;
  settings_ = PartSettings{};
  allocator_.configure(settings_.voiceMode, settings_.voiceCount);
  applySettings(current);
  gainL_.reset(gainL_.target());
  gainR_.reset(gainR_.target());
}

void Part::applySettings(const PartSettings& s) {
  const bool configChanged =
      s.voiceMode != settings_.voiceMode || s.voiceCount != settings_.voiceCount;
  const bool alwaysOnChanged = s.alwaysOn != alwaysOn_;
  const bool baseChanged = s.baseNote != settings_.baseNote;
  const bool engineChanged = s.engine != settings_.engine;
  const bool filterChanged = s.filterEngine != settings_.filterEngine;
  const bool shaperChanged = s.shaperEngine != settings_.shaperEngine;
  settings_ = s;

  if (configChanged) perform(allocator_.configure(s.voiceMode, s.voiceCount));
  if (alwaysOnChanged) {
    if (s.alwaysOn) {
      perform(allocator_.allNotesOff());
      alwaysOnNotes_.clear();
      alwaysOn_ = true;
      if (!voices_[0]->active()) modulation_.startVoice(0);
      voices_[0]->trigger(s.baseNote, 1.0f);
      controlValid_[0] = false;
      voiceVelocity_[0] = 1.0f;
      voiceKey_[0] = s.baseNote;
    } else {
      alwaysOn_ = false;
      alwaysOnNotes_.clear();
      voices_[0]->release();
    }
  } else if (alwaysOn_ && baseChanged && alwaysOnNotes_.empty()) {
    voices_[0]->glideTo(s.baseNote);
  }
  if (engineChanged) {
    for (auto& voice : voices_) voice->setEngine(s.engine);
  }
  if (filterChanged) {
    for (auto& voice : voices_) voice->setFilterEngine(s.filterEngine);
  }
  if (shaperChanged) {
    for (auto& voice : voices_) voice->setShaperEngine(s.shaperEngine);
  }
  for (int slot = 0; slot < kNumModSlots; ++slot) modulation_.configure(slot, s.modEngine[slot], s.modPerPart[slot]);

  const float glide = glideSeconds(s.glide);
  for (auto& voice : voices_) voice->setGlideSeconds(glide);
  updatePitchOffset();
  // The modulated path keeps its last Part Level (preflight ruling G5): resetting it here, once per
  // host block, would make the level depend on the host's block sizes.
  updateGains(modulated_ ? outputLevel_ : s.level);
}

bool Part::isMemberChannel(int channel) const {
  return settings_.mpe != 0 && channel >= settings_.mpeFirst && channel <= settings_.mpeLast &&
         channel != settings_.mpeMaster;
}

bool Part::acceptsChannel(int channel) const {
  if (channel < 0) return true;
  if (settings_.mpe != 0) return channel == settings_.mpeMaster || isMemberChannel(channel);
  return settings_.midiChannel == 0 || channel == settings_.midiChannel - 1;
}

bool Part::acceptsKey(int key) const {
  const int low = std::min(settings_.keyLow, settings_.keyHigh);
  const int high = std::max(settings_.keyLow, settings_.keyHigh);
  return key >= low && key <= high;
}

void Part::noteOn(int key, float velocity, int channel) {
  if (key >= 0 && key < 128) keyChannel_[key] = std::clamp(channel, 0, kChannels - 1);
  if (alwaysOn_) {
    // Always on (spec §5.3): notes move the pitch but do not retrigger the voice. The per-voice
    // mod engines' gate follows the keys: the first key down triggers them (provisional ruling 8).
    if (alwaysOnNotes_.empty()) {
      startNote(0, key, velocity);
    } else {
      moveNote(0, key);
    }
    alwaysOnNotes_.noteOn(key, velocity);
    voices_[0]->glideTo(key);
    return;
  }
  perform(allocator_.noteOn(key, velocity));
}

void Part::noteOff(int key, float releaseVelocity) {
  if (key >= 0 && key < 128) keyRelease_[key] = clamp01(releaseVelocity);
  if (alwaysOn_) {
    const bool wasTop = !alwaysOnNotes_.empty() && alwaysOnNotes_.topKey() == key;
    alwaysOnNotes_.noteOff(key);
    if (alwaysOnNotes_.empty()) {
      voices_[0]->glideTo(settings_.baseNote);
      voiceRelease_[0] = clamp01(releaseVelocity);
    } else if (wasTop) {
      voices_[0]->glideTo(alwaysOnNotes_.topKey());
      moveNote(0, alwaysOnNotes_.topKey());
    }
    return;
  }
  perform(allocator_.noteOff(key));
}

void Part::pitchBend(float value) {
  bend_ = std::clamp(value, -1.0f, 1.0f);
  updatePitchOffset();
}

void Part::sustain(bool down) {
  if (!alwaysOn_) perform(allocator_.setSustain(down));
}

void Part::allNotesOff() {
  if (alwaysOn_) {
    alwaysOnNotes_.clear();
    voices_[0]->glideTo(settings_.baseNote);
    return;
  }
  perform(allocator_.allNotesOff());
}

void Part::setController(PartController controller, float value) {
  value = clamp01(value);
  switch (controller) {
    case PartController::ModWheel: modWheel_ = value; break;
    case PartController::Slide: slide_ = value; break;
    case PartController::ChannelPressure: pressure_ = value; break;
    case PartController::CcA: cc_[0] = value; break;
    case PartController::CcB: cc_[1] = value; break;
    case PartController::CcC: cc_[2] = value; break;
    case PartController::CcD: cc_[3] = value; break;
  }
}

namespace {

void setExpression(Expression e, float value, float* bend, float* pressure, float* slide) {
  if (e == Expression::Bend) {
    *bend = std::isfinite(value) ? value : 0.0f;
  } else if (e == Expression::Pressure) {
    *pressure = clamp01(value);
  } else {
    *slide = clamp01(value);
  }
}

}  // namespace

void Part::memberExpression(int channel, Expression expression, float value) {
  if (channel < 0 || channel >= kChannels) return;
  setExpression(expression, value, &channelBend_[channel], &channelPressure_[channel], &channelSlide_[channel]);
  for (int v = 0; v < kVoices; ++v) {
    if (voices_[size_t(v)]->active() && voiceChannel_[v] == channel) {
      setExpression(expression, value, &voiceBend_[v], &voicePressure_[v], &voiceSlide_[v]);
    }
  }
}

void Part::noteExpression(int channel, int key, Expression expression, float value) {
  for (int v = 0; v < kVoices; ++v) {
    if (voices_[size_t(v)]->active() && (key < 0 || voiceKey_[v] == key) &&
        (channel < 0 || voiceChannel_[v] == channel)) {
      setExpression(expression, value, &voiceBend_[v], &voicePressure_[v], &voiceSlide_[v]);
    }
  }
}

void Part::process(const float* const* in, float* const* out, const ParamValues& source, int n) {
  SlotParams params;
  params.source = source;
  process(VoiceInputs{in, nullptr}, out, params, n);
}

void Part::delayInputSide(const float* const* input, int n) {
  const int length = int(inputDelay_.size());
  for (int i = 0; i < n; ++i) {
    const float x = input ? 0.5f * (input[0][i] + input[1][i]) : 0.0f;
    if (length == 0) {
      inputSide_[size_t(i)] = x;
      continue;
    }
    inputSide_[size_t(i)] = inputDelay_[size_t(inputDelayPos_)];
    inputDelay_[size_t(inputDelayPos_)] = x;
    if (++inputDelayPos_ == length) inputDelayPos_ = 0;
  }
}

void Part::process(const VoiceInputs& in, float* const* out, const SlotParams& params, int n) {
  if (modulated_) {  // back from the modulated path: the unmodulated level
    modulated_ = false;
    updateGains(settings_.level);
  }
  std::fill(mix_[0].begin(), mix_[0].begin() + n, 0.0f);
  std::fill(mix_[1].begin(), mix_[1].begin() + n, 0.0f);
  float* mix[2] = {mix_[0].data(), mix_[1].data()};
  delayInputSide(in.input, n);
  VoiceInputs voiceIn = in;
  voiceIn.inputSide = inputSide_.data();
  for (int v = 0; v < kVoices; ++v) {
    voices_[size_t(v)]->process(voiceIn, mix, params, n);
    if (allocator_.sounding(v) && !voices_[size_t(v)]->active()) allocator_.voiceIdle(v);
  }
  writeOutput(out, 0, n, false);
}

ModVoiceInputs Part::voiceInputs(int v) const {
  const Voice& voice = *voices_[size_t(v)];
  ModVoiceInputs m;
  m.active = voice.active();
  // Always on: the voice's gate stays high (its Amp open); its mod engines follow the keys.
  m.gate = alwaysOn_ && v == 0 ? !alwaysOnNotes_.empty() : voice.gate();
  m.retrigger = voiceRetrigger_[v];
  m.note = voice.currentNote();
  m.velocity = voiceVelocity_[v];
  m.releaseVelocity = voiceRelease_[v];
  m.random = voiceRandom_[v];
  m.pressure = voicePressure_[v];
  m.slide = voiceSlide_[v];
  m.bend = voiceBend_[v];
  return m;
}

ModPartInputs Part::partInputs(const PartParams& params, int offset) const {
  ModPartInputs p;
  p.modWheel = modWheel_;
  p.pitchBend = bend_;
  p.channelPressure = pressure_;
  p.slide = slide_;
  for (int i = 0; i < 4; ++i) p.cc[i] = cc_[i];
  const ModTiming& t = params.timing;
  p.noteBendRange = float(settings_.mpeBendRange);
  p.gate = partGate_;
  p.retrigger = partRetrigger_;
  p.beat = t.beatAt(offset);
  p.transportPhase = float(BeatClock::barPhaseAt(p.beat, t.barStart, t.beatsPerBar));
  p.beatsPerSample = t.beatsPerSample;
  p.beatJump = pendingJump_;
  p.lastVoice = lastVoice_;
  return p;
}

void Part::controlVoice(int v, const PartParams& params, const ModPartInputs& part, int length) {
  VoiceControl& c = controls_[v];
  modulation_.processVoice(v, params.mod, part, voiceInputs(v), params.slots, &c, length);
  c.gain = params.voiceGain;
  if (voices_[size_t(v)]->gate()) voiceInputScale_[v] = inputScale_;
  c.sourceGain = sourceTrim(settings_.engine) * voiceInputScale_[v];  // gain staging (rulings 25, 26)
  voices_[size_t(v)]->beginTick(c, length);
  controlValid_[v] = true;
  voiceRetrigger_[v] = false;
}

void Part::startTick(const PartParams& params, int offset) {
  // The part-scope gate: any key held (Always on: any key down), triggering on the first key.
  bool gate = false;
  if (alwaysOn_) {
    gate = !alwaysOnNotes_.empty();
  } else {
    for (const auto& voice : voices_) gate = gate || voice->gate();
  }
  if (gate && !partGate_) partRetrigger_ = true;
  partGate_ = gate;
  // Audio Input voices all carry the same Input: scale each by the number with an open gate, so
  // a chord plays the Input at one voice's level.
  int open = 0;
  for (const auto& voice : voices_) open += voice->active() && voice->gate() ? 1 : 0;
  if (settings_.engine == int(SourceEngine::AudioInput)) {
    const float target = 1.0f / float(std::max(1, open));
    for (int i = 0; i < Voice::kControlBlock; ++i) inputScale_ = inputScaleSmoother_.next(target);  // per sample
  } else {
    inputScaleSmoother_.reset();  // snaps to 1 on the next Audio Input tick's first target
    inputScale_ = 1.0f;
  }
  ModVoiceInputs voices[kVoices];
  for (int v = 0; v < kVoices; ++v) voices[v] = voiceInputs(v);
  const ModPartInputs part = partInputs(params, offset);
  modulation_.processPart(params.mod, part, voices, Voice::kControlBlock);
  partRetrigger_ = false;
  pendingJump_ = false;
  partLevel_ = modulation_.partLevel(params.mod, settings_.level);
  for (int v = 0; v < kVoices; ++v) {
    controlValid_[v] = false;
    if (voices_[size_t(v)]->active()) controlVoice(v, params, part, Voice::kControlBlock);
  }
}

void Part::process(const VoiceInputs& in, float* const* out, const PartParams& params, int n) {
  std::fill(mix_[0].begin(), mix_[0].begin() + n, 0.0f);
  std::fill(mix_[1].begin(), mix_[1].begin() + n, 0.0f);
  delayInputSide(in.input, n);
  if (!modulated_) {  // from the unmodulated path (or a reset): its level until the first tick's arrives
    outputLevel_ = settings_.level;
    std::fill(levelLine_.begin(), levelLine_.end(), outputLevel_);
    levelLinePos_ = 0;
  }
  modulated_ = true;
  pendingJump_ = pendingJump_ || params.timing.jump;
  for (int done = 0; done < n;) {
    // Modulation runs on a fixed grid of 16-sample ticks, whatever the host's block sizes.
    if (tickPos_ == 0) startTick(params, done);
    const int count = std::min(Voice::kControlBlock - tickPos_, n - done);
    for (int v = 0; v < kVoices; ++v) {
      // A voice that started since the tick began gets controls for the rest of the tick. A
      // pending transport jump is not passed on here: the next tick's start delivers it to every
      // voice, this one included, so it arrives once.
      if (voices_[size_t(v)]->active() && !controlValid_[v]) {
        ModPartInputs part = partInputs(params, done);
        part.beatJump = false;
        controlVoice(v, params, part, Voice::kControlBlock - tickPos_);
      }
    }
    const float* input[2] = {in.input ? in.input[0] + done : nullptr, in.input ? in.input[1] + done : nullptr};
    const float* other[2] = {in.other ? in.other[0] + done : nullptr, in.other ? in.other[1] + done : nullptr};
    VoiceInputs slice;
    slice.input = in.input ? input : nullptr;
    slice.other = in.other ? other : nullptr;
    slice.inputSide = inputSide_.data() + done;
    float* mix[2] = {mix_[0].data() + done, mix_[1].data() + done};
    for (int v = 0; v < kVoices; ++v) {
      voices_[size_t(v)]->render(slice, mix, count);
      if (allocator_.sounding(v) && !voices_[size_t(v)]->active()) allocator_.voiceIdle(v);
    }
    // The part's gains per slice, so a Part Level change lands at its tick whatever the block.
    writeOutput(out, done, count, true);
    tickPos_ = (tickPos_ + count) % Voice::kControlBlock;
    done += count;
  }
}

void Part::writeOutput(float* const* out, int from, int n, bool modulated) {
  for (int i = from; i < from + n; ++i) {
    if (modulated) {
      const float level = levelLine_.empty() ? partLevel_ : levelLine_[size_t(levelLinePos_)];
      if (!levelLine_.empty()) {
        levelLine_[size_t(levelLinePos_)] = partLevel_;
        if (++levelLinePos_ == int(levelLine_.size())) levelLinePos_ = 0;
      }
      if (level != outputLevel_) {
        outputLevel_ = level;
        updateGains(level);
      }
    }
    const float gl = gainL_.next();
    const float gr = gainR_.next();
    if (settings_.mono) {
      const float centre = 0.5f * (mix_[0][size_t(i)] + mix_[1][size_t(i)]) * gl;
      out[0][i] = centre;
      out[1][i] = centre;
    } else {
      out[0][i] = mix_[0][size_t(i)] * gl;
      out[1][i] = mix_[1][size_t(i)] * gr;
    }
  }
}

int Part::activeVoiceCount() const {
  int count = 0;
  for (const auto& voice : voices_) count += voice->active() ? 1 : 0;
  return count;
}

void Part::startNote(int v, int key, float velocity) {
  moveNote(v, key);
  voiceVelocity_[v] = clamp01(velocity);
  voiceRelease_[v] = 0.0f;
  random_ ^= random_ << 13;  // xorshift32: a new Random source value per note
  random_ ^= random_ >> 17;
  random_ ^= random_ << 5;
  voiceRandom_[v] = float(random_ >> 8) / 16777216.0f;
  voiceRetrigger_[v] = true;
  lastVoice_ = v;
}

void Part::moveNote(int v, int key) {
  const int channel = key >= 0 && key < 128 ? keyChannel_[key] : 0;
  voiceKey_[v] = key;
  voiceChannel_[v] = channel;
  // An MPE member channel's expression sent before the note belongs to it (provisional ruling 11);
  // a note on any other channel starts with none, so a legato note never keeps the previous one's.
  const bool member = isMemberChannel(channel);
  voiceBend_[v] = member ? channelBend_[channel] : 0.0f;
  voicePressure_[v] = member ? channelPressure_[channel] : 0.0f;
  voiceSlide_[v] = member ? channelSlide_[channel] : 0.0f;
}

void Part::perform(const VoiceActions& actions) {
  for (int i = 0; i < actions.count; ++i) {
    const VoiceAction& a = actions.items[i];
    Voice& voice = *voices_[size_t(a.voice)];
    switch (a.type) {
      case VoiceAction::Type::Trigger:
        if (!voice.active()) {
          modulation_.startVoice(a.voice);  // a voice from silence: fresh mod engines
          controlValid_[a.voice] = false;
        }
        voice.trigger(a.key, a.velocity);
        startNote(a.voice, a.key, a.velocity);
        break;
      case VoiceAction::Type::Glide:
        // Legato: the voice glides on without retriggering its mod engines; the new key's channel
        // and expression replace the previous note's.
        voice.glideTo(a.key);
        moveNote(a.voice, a.key);
        break;
      case VoiceAction::Type::Release:
        voice.release();
        voiceRelease_[a.voice] = a.key >= 0 && a.key < 128 ? keyRelease_[a.key] : kDefaultReleaseVelocity;
        break;
    }
  }
}

void Part::updatePitchOffset() {
  const float offset = float(settings_.transpose) + bend_ * float(settings_.bendRange);
  for (auto& voice : voices_) voice->setPitchOffset(offset);
}

void Part::updateGains(float level) {
  if (settings_.mono) {
    gainL_.setTarget(level);
    gainR_.setTarget(level);
    return;
  }
  gainL_.setTarget(level * std::min(1.0f, 1.0f - settings_.pan));
  gainR_.setTarget(level * std::min(1.0f, 1.0f + settings_.pan));
}

}  // namespace eg13
