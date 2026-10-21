#include "part/part.h"

#include <algorithm>

#include "synth/param_table.h"
#include "voice/processor_factory.h"

namespace eg13 {

namespace {
constexpr double kGainSmoothingSeconds = 0.01;
}  // namespace

Part::Part() : Part(makeAllSources, makeAllFilters, makeAllShapers) {}

Part::Part(SlotFactory sources, SlotFactory filters, SlotFactory shapers) {
  for (int v = 0; v < kVoices; ++v) voices_.push_back(std::make_unique<Voice>(sources(), filters(), shapers()));
}

Part::Part(SourceFactory factory) {
  for (int v = 0; v < kVoices; ++v) voices_.push_back(std::make_unique<Voice>(factory()));
}

void Part::prepare(double hostRate, int maxBlock, Voice::Alignment alignment) {
  for (auto& voice : voices_) voice->prepare(hostRate, maxBlock, alignment);
  for (auto& buffer : mix_) buffer.assign(size_t(maxBlock), 0.0f);
  int sourceLatency = 0;
  for (auto& voice : voices_) sourceLatency = std::max(sourceLatency, voice->sourceLatencySamples());
  inputDelay_.assign(size_t(sourceLatency), 0.0f);
  inputSide_.assign(size_t(maxBlock), 0.0f);
  gainL_.prepare(hostRate, kGainSmoothingSeconds);
  gainR_.prepare(hostRate, kGainSmoothingSeconds);
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
      voices_[0]->trigger(s.baseNote, 1.0f);
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

  const float glide = glideSeconds(s.glide);
  for (auto& voice : voices_) voice->setGlideSeconds(glide);
  updatePitchOffset();
  updateGains();
}

bool Part::acceptsChannel(int channel) const {
  return settings_.midiChannel == 0 || channel < 0 || channel == settings_.midiChannel - 1;
}

bool Part::acceptsKey(int key) const {
  const int low = std::min(settings_.keyLow, settings_.keyHigh);
  const int high = std::max(settings_.keyLow, settings_.keyHigh);
  return key >= low && key <= high;
}

void Part::noteOn(int key, float velocity) {
  if (alwaysOn_) {
    alwaysOnNotes_.noteOn(key, velocity);
    voices_[0]->glideTo(key);
    return;
  }
  perform(allocator_.noteOn(key, velocity));
}

void Part::noteOff(int key) {
  if (alwaysOn_) {
    const bool wasTop = !alwaysOnNotes_.empty() && alwaysOnNotes_.topKey() == key;
    alwaysOnNotes_.noteOff(key);
    if (alwaysOnNotes_.empty()) {
      voices_[0]->glideTo(settings_.baseNote);
    } else if (wasTop) {
      voices_[0]->glideTo(alwaysOnNotes_.topKey());
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
  for (int i = 0; i < n; ++i) {
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

void Part::perform(const VoiceActions& actions) {
  for (int i = 0; i < actions.count; ++i) {
    const VoiceAction& a = actions.items[i];
    Voice& voice = *voices_[size_t(a.voice)];
    switch (a.type) {
      case VoiceAction::Type::Trigger:
        voice.trigger(a.key, a.velocity);
        break;
      case VoiceAction::Type::Glide:
        voice.glideTo(a.key);
        break;
      case VoiceAction::Type::Release:
        voice.release();
        break;
    }
  }
}

void Part::updatePitchOffset() {
  const float offset = float(settings_.transpose) + bend_ * float(settings_.bendRange);
  for (auto& voice : voices_) voice->setPitchOffset(offset);
}

void Part::updateGains() {
  if (settings_.mono) {
    gainL_.setTarget(settings_.level);
    gainR_.setTarget(settings_.level);
    return;
  }
  gainL_.setTarget(settings_.level * std::min(1.0f, 1.0f - settings_.pan));
  gainR_.setTarget(settings_.level * std::min(1.0f, 1.0f + settings_.pan));
}

}  // namespace eg13
