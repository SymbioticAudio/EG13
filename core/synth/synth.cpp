#include "synth/synth.h"

#include <algorithm>
#include <cmath>
#include <thread>

namespace eg13 {

namespace {
constexpr double kGainSmoothingSeconds = 0.005;
}  // namespace

Synth::Synth(PluginKind kind, SourceFactory factory) : kind_(kind) {
  for (auto& part : parts_) part = std::make_unique<Part>(factory);
  store(defaultSnapshot());
}

void Synth::prepare(double sampleRate, int maxBlock) {
  applyPending();
  for (auto& part : parts_) part->prepare(sampleRate, maxBlock);
  latency_ = parts_[0]->latencySamples();
  for (auto& line : dryDelay_) line.assign(size_t(latency_), 0.0f);
  for (auto& part : partOut_) {
    for (auto& buffer : part) buffer.assign(size_t(maxBlock), 0.0f);
  }
  masterGain_.prepare(sampleRate, kGainSmoothingSeconds);
  dryGain_.prepare(sampleRate, kGainSmoothingSeconds);
  reset();
}

void Synth::setProcessing(bool active) { processing_.store(active, std::memory_order_release); }

void Synth::reset() {
  for (int p = 0; p < kNumParts; ++p) {
    parts_[size_t(p)]->applySettings(readPartSettings(p));
    parts_[size_t(p)]->reset();
  }
  for (auto& line : dryDelay_) std::fill(line.begin(), line.end(), 0.0f);
  dryPos_ = 0;
  for (auto& channel : routed_) std::fill(std::begin(channel), std::end(channel), uint8_t{0});
  masterGain_.reset(load(masterParamIndex(MasterParam::Level)));
  dryGain_.reset(load(masterParamIndex(MasterParam::Dry)));
}

int Synth::clampChannel(int channel) { return std::clamp(channel, 0, 15); }

void Synth::noteOn(int channel, int key, float velocity) {
  if (key < 0 || key > 127) return;
  channel = clampChannel(channel);
  if (velocity <= 0.0f) {
    noteOff(channel, key);
    return;
  }
  const auto mode = PartMode(int(load(masterParamIndex(MasterParam::Mode))));
  const int split = int(load(masterParamIndex(MasterParam::SplitNote)));
  uint8_t mask = 0;
  for (int p = 0; p < kNumParts; ++p) {
    Part& part = *parts_[size_t(p)];
    if (mode == PartMode::Single && p != 0) continue;
    if (!part.acceptsChannel(channel) || !part.acceptsKey(key)) continue;
    if (mode == PartMode::Split && ((p == 0) != (key < split))) continue;
    part.noteOn(key, velocity);
    mask = uint8_t(mask | (1u << p));
  }
  routed_[channel][key] = uint8_t(routed_[channel][key] | mask);
}

void Synth::noteOff(int channel, int key) {
  if (key < 0 || key > 127) return;
  const int first = channel < 0 ? 0 : clampChannel(channel);
  const int last = channel < 0 ? 15 : first;
  for (int ch = first; ch <= last; ++ch) {
    const uint8_t mask = routed_[ch][key];
    for (int p = 0; p < kNumParts; ++p) {
      if (mask & (1u << p)) parts_[size_t(p)]->noteOff(key);
    }
    routed_[ch][key] = 0;
  }
}

void Synth::pitchBend(int channel, float value) {
  for (auto& part : parts_) {
    if (part->acceptsChannel(channel)) part->pitchBend(value);
  }
}

void Synth::controlChange(int channel, int cc, int value) {
  if (cc == 64) {
    for (auto& part : parts_) {
      if (part->acceptsChannel(channel)) part->sustain(value >= 64);
    }
  } else if (cc == 120 || cc == 123) {
    for (auto& part : parts_) {
      if (part->acceptsChannel(channel)) part->allNotesOff();
    }
    if (channel >= 0) {
      std::fill(std::begin(routed_[clampChannel(channel)]), std::end(routed_[clampChannel(channel)]),
                uint8_t{0});
    }
  }
}

void Synth::allNotesOff() {
  for (auto& part : parts_) part->allNotesOff();
  for (auto& channel : routed_) std::fill(std::begin(channel), std::end(channel), uint8_t{0});
}

bool Synth::setParam(uint32_t id, double value) {
  const int index = paramIndex(id);
  if (index < 0 || std::isnan(value)) return false;
  values_[size_t(index)].store(clampParam(size_t(index), value), std::memory_order_relaxed);
  return true;
}

double Synth::param(uint32_t id) const {
  const int index = paramIndex(id);
  if (index < 0) return 0.0;
  if (pending_.load(std::memory_order_acquire)) return double(pendingValues_[size_t(index)]);
  return double(load(size_t(index)));
}

ParamSnapshot Synth::defaultSnapshot() const {
  ParamSnapshot values{};
  for (size_t i = 0; i < kParamCount; ++i) values[i] = paramDefault(i, kind_);
  return values;
}

ParamSnapshot Synth::snapshot() const {
  ParamSnapshot values{};
  const bool pending = pending_.load(std::memory_order_acquire);
  for (size_t i = 0; i < kParamCount; ++i) values[i] = pending ? pendingValues_[i] : load(i);
  return values;
}

void Synth::store(const ParamSnapshot& values) {
  for (size_t i = 0; i < kParamCount; ++i) values_[i].store(values[i], std::memory_order_relaxed);
}

void Synth::loadParams(const ParamSnapshot& values) {
  ParamSnapshot clamped{};
  for (size_t i = 0; i < kParamCount; ++i) clamped[i] = clampParam(i, double(values[i]));

  // Wait for the audio thread to take a previous snapshot.
  while (pending_.load(std::memory_order_acquire) && processing_.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  if (!processing_.load(std::memory_order_acquire)) {
    store(clamped);
    pending_.store(false, std::memory_order_release);
    return;
  }
  pendingValues_ = clamped;
  pending_.store(true, std::memory_order_release);
}

void Synth::applyPending() {
  if (!pending_.load(std::memory_order_acquire)) return;
  store(pendingValues_);
  pending_.store(false, std::memory_order_release);
}

PartSettings Synth::readPartSettings(int part) const {
  auto v = [this, part](PartParam p) { return load(partParamIndex(part, p)); };
  PartSettings s;
  s.level = v(PartParam::Level);
  s.pan = v(PartParam::Pan);
  s.glide = v(PartParam::Glide);
  s.transpose = int(v(PartParam::Transpose));
  s.baseNote = int(v(PartParam::BaseNote));
  s.midiChannel = int(v(PartParam::MidiChannel));
  s.keyLow = int(v(PartParam::KeyLow));
  s.keyHigh = int(v(PartParam::KeyHigh));
  s.voiceMode = VoiceMode(int(v(PartParam::VoiceMode)));
  s.voiceCount = int(v(PartParam::VoiceCount));
  s.alwaysOn = v(PartParam::Trigger) >= 0.5f;
  s.bendRange = int(v(PartParam::BendRange));
  s.mono = v(PartParam::Mono) >= 0.5f;
  s.output = int(v(PartParam::Output));
  return s;
}

void Synth::process(const float* const* in, float* const* mainOut, float* const* bus2Out, int n) {
  applyPending();
  for (int p = 0; p < kNumParts; ++p) parts_[size_t(p)]->applySettings(readPartSettings(p));
  masterGain_.setTarget(load(masterParamIndex(MasterParam::Level)));
  dryGain_.setTarget(load(masterParamIndex(MasterParam::Dry)));
  const auto mode = PartMode(int(load(masterParamIndex(MasterParam::Mode))));

  bool toBus2[kNumParts];
  for (int p = 0; p < kNumParts; ++p) {
    ParamValues source;
    for (int i = 0; i < int(kSourceParamCount); ++i) source.p[i] = load(sourceParamIndex(p, i));
    float* out[2] = {partOut_[p][0].data(), partOut_[p][1].data()};
    parts_[size_t(p)]->process(out, source, n);
    toBus2[p] = bus2Out != nullptr && parts_[size_t(p)]->settings().output == 1;
  }

  for (int i = 0; i < n; ++i) {
    float main[2] = {0.0f, 0.0f};
    float bus[2] = {0.0f, 0.0f};
    for (int p = 0; p < kNumParts; ++p) {
      if (mode == PartMode::Single && p != 0) continue;
      float* target = toBus2[p] ? bus : main;
      target[0] += partOut_[p][0][size_t(i)];
      target[1] += partOut_[p][1][size_t(i)];
    }
    const float gain = masterGain_.next();
    const float dry = dryGain_.next();
    for (int ch = 0; ch < 2; ++ch) {
      float delayed = 0.0f;
      if (in != nullptr) {
        const float input = in[ch][i];  // read before mainOut is written (may alias)
        if (latency_ == 0) {
          delayed = input;
        } else {
          delayed = dryDelay_[ch][dryPos_];
          dryDelay_[ch][dryPos_] = input;
        }
      }
      mainOut[ch][i] = main[ch] * gain + delayed * dry;
      if (bus2Out != nullptr) bus2Out[ch][i] = bus[ch] * gain;
    }
    if (in != nullptr && latency_ > 0) dryPos_ = (dryPos_ + 1) % size_t(latency_);
  }
}

}  // namespace eg13
