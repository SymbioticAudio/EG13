#include "synth/synth.h"

#include <algorithm>
#include <cmath>
#include <thread>

namespace eg13 {

namespace {
constexpr double kGainSmoothingSeconds = 0.01;
}  // namespace

Synth::Synth(PluginKind kind) : kind_(kind) {
  for (auto& part : parts_) part = std::make_unique<Part>();
  loadParams(defaultSnapshot());
}

Synth::Synth(PluginKind kind, SourceFactory factory) : kind_(kind) {
  for (auto& part : parts_) part = std::make_unique<Part>(factory);
  loadParams(defaultSnapshot());
}

void Synth::prepare(double sampleRate, int maxBlock) {
  applyPending();
  // The instrument lines up note onsets; EG13 FX lines up the input path with Dry.
  const auto alignment = kind_ == PluginKind::Effect ? Voice::Alignment::Input : Voice::Alignment::Notes;
  for (auto& part : parts_) part->prepare(sampleRate, maxBlock, alignment);
  latency_ = parts_[0]->latencySamples();
  for (auto& line : dryDelay_) line.assign(size_t(latency_), 0.0f);
  for (auto& part : partOut_) {
    for (auto& buffer : part) buffer.assign(size_t(maxBlock), 0.0f);
  }
  for (auto& part : partIn_) {
    for (auto& buffer : part) buffer.assign(size_t(maxBlock), 0.0f);
  }
  feedDelay_ = size_t(std::min(maxBlock, kFeedBlock));
  for (auto& ring : feedRing_) ring.assign(2 * feedDelay_, 0.0f);
  feedPos_ = 0;
  masterGain_.prepare(sampleRate, kGainSmoothingSeconds);
  dryGain_.prepare(sampleRate, kGainSmoothingSeconds);
  partBMute_.prepare(sampleRate, kGainSmoothingSeconds);
  reset();
}

void Synth::setActive(bool active) {
  if (!active) applyPending();  // the audio thread has stopped
  active_.store(active, std::memory_order_release);
}

void Synth::beginBlock() { applyPending(); }

void Synth::reset() {
  for (int p = 0; p < kNumParts; ++p) {
    parts_[size_t(p)]->applySettings(readPartSettings(p));
    parts_[size_t(p)]->reset();
  }
  for (auto& line : dryDelay_) std::fill(line.begin(), line.end(), 0.0f);
  dryPos_ = 0;
  for (auto& ring : feedRing_) std::fill(ring.begin(), ring.end(), 0.0f);
  feedPos_ = 0;
  for (auto& channel : routed_) std::fill(std::begin(channel), std::end(channel), uint8_t{0});
  masterGain_.reset(load(masterParamIndex(MasterParam::Level)));
  dryGain_.reset(load(masterParamIndex(MasterParam::Dry)));
  partBMute_.reset(PartMode(int(load(masterParamIndex(MasterParam::Mode)))) == PartMode::Single ? 0.0f : 1.0f);
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
  const float clamped = clampParam(size_t(index), value);
  // Switch at the selector event, not at the next process: P1..P8 events after
  // it in the same block (or flush) then land on the new engine. The P values
  // are swapped before the selector is stored, so a main-thread snapshot() or
  // engineStash() never sees the new selector with the old engine's values.
  // While the 8 P stores are in flight it can still see the old selector with
  // some new P values; that read is benign (display, or a state save the host
  // repeats) and the next one is consistent.
  for (int part = 0; part < kNumParts; ++part) {
    if (size_t(index) == sourceEngineParamIndex(part)) switchEngine(part, sourceEngineFromSelector(clamped));
  }
  values_[size_t(index)].store(clamped, std::memory_order_relaxed);
  return true;
}

double Synth::param(uint32_t id) const {
  const int index = paramIndex(id);
  if (index < 0) return 0.0;
  if (hasPending()) return double(pendingValues_[size_t(index)]);
  return double(load(size_t(index)));
}

ParamSnapshot Synth::defaultSnapshot() const {
  ParamSnapshot values{};
  for (size_t i = 0; i < kParamCount; ++i) values[i] = paramDefault(i, kind_);
  return values;
}

ParamSnapshot Synth::snapshot() const {
  ParamSnapshot values{};
  const bool pending = hasPending();
  for (size_t i = 0; i < kParamCount; ++i) values[i] = pending ? pendingValues_[i] : load(i);
  return values;
}

void Synth::store(const ParamSnapshot& values) {
  for (size_t i = 0; i < kParamCount; ++i) values_[i].store(values[i], std::memory_order_relaxed);
}

EngineStash Synth::defaultStash() {
  EngineStash stash{};
  for (int part = 0; part < kNumParts; ++part) {
    for (int e = 0; e < kNumSourceEngines; ++e) {
      const EngineInfo& info = sourceEngineInfo(e);
      for (int p = 0; p < int(kSourceParamCount); ++p) {
        stash[stashIndex(part, e, p)] = p < info.paramCount ? info.params[p].defaultValue : 0.0f;
      }
    }
  }
  return stash;
}

void Synth::loadParams(const ParamSnapshot& values, const EngineStash* stash) {  // main thread
  ParamSnapshot clamped{};
  for (size_t i = 0; i < kParamCount; ++i) clamped[i] = clampParam(i, double(values[i]));
  const EngineStash s = stash ? *stash : defaultStash();
  if (!active_.load(std::memory_order_acquire)) {  // no audio thread can run
    store(clamped);
    for (size_t i = 0; i < kStashSize; ++i) stash_[i].store(s[i], std::memory_order_relaxed);
    hostUpdateCount_ = 0;  // superseded by the load
    for (int part = 0; part < kNumParts; ++part) {
      currentEngine_[size_t(part)] = sourceEngineFromSelector(clamped[sourceEngineParamIndex(part)]);
    }
    pendingState_.store(kIdle, std::memory_order_release);
    return;
  }
  for (;;) {
    int expected = kReady;  // replace a snapshot the audio thread has not taken yet
    if (pendingState_.compare_exchange_strong(expected, kWriting, std::memory_order_acq_rel)) break;
    expected = kIdle;
    if (pendingState_.compare_exchange_strong(expected, kWriting, std::memory_order_acq_rel)) break;
    std::this_thread::yield();  // kApplying: the audio thread is copying the values
  }
  pendingValues_ = clamped;
  pendingStash_ = s;
  pendingState_.store(kReady, std::memory_order_release);
}

void Synth::applyPending() {  // audio thread, or main thread while inactive
  int expected = kReady;
  if (!pendingState_.compare_exchange_strong(expected, kApplying, std::memory_order_acq_rel)) return;
  store(pendingValues_);
  for (size_t i = 0; i < kStashSize; ++i) stash_[i].store(pendingStash_[i], std::memory_order_relaxed);
  hostUpdateCount_ = 0;  // switches from before the load are superseded by it
  // The snapshot's P values belong to its engines: adopt them without swapping.
  for (int part = 0; part < kNumParts; ++part) {
    currentEngine_[size_t(part)] = sourceEngineFromSelector(pendingValues_[sourceEngineParamIndex(part)]);
  }
  pendingState_.store(kIdle, std::memory_order_release);
}

EngineStash Synth::engineStash() const {  // main thread
  EngineStash s{};
  const bool pending = hasPending();
  for (size_t i = 0; i < kStashSize; ++i) {
    s[i] = pending ? pendingStash_[i] : stash_[i].load(std::memory_order_relaxed);
  }
  for (int part = 0; part < kNumParts; ++part) {
    const int e = engine(part);
    for (int p = 0; p < int(kSourceParamCount); ++p) {
      s[stashIndex(part, e, p)] = float(param(paramSpec(sourceParamIndex(part, p)).id));
    }
  }
  return s;
}

int Synth::engine(int part) const {
  return sourceEngineFromSelector(param(paramSpec(sourceEngineParamIndex(part)).id));
}

const char* Synth::paramName(size_t index) const {
  const int part = index < size_t(kNumParts) * kParamsPerPart ? int(index / kParamsPerPart) : 0;
  return eg13::paramName(index, engine(part));
}

void Synth::formatParam(size_t index, double value, char* buf, size_t size) const {
  const int part = index < size_t(kNumParts) * kParamsPerPart ? int(index / kParamsPerPart) : 0;
  eg13::formatParam(index, value, buf, size, engine(part));
}

int Synth::takeHostUpdates(HostUpdate* out, int max) {  // audio thread
  const int count = std::clamp(max, 0, hostUpdateCount_);
  for (int i = 0; i < count; ++i) out[i] = hostUpdates_[i];
  for (int i = count; i < hostUpdateCount_; ++i) hostUpdates_[i - count] = hostUpdates_[i];
  hostUpdateCount_ -= count;
  return count;
}

// Same threading as setParam. Saves the old engine's P1..P8, loads the new
// engine's and queues them for the host.
void Synth::switchEngine(int part, int next) {
  const int previous = currentEngine_[size_t(part)];
  if (next == previous) return;
  for (int p = 0; p < int(kSourceParamCount); ++p) {
    const size_t index = sourceParamIndex(part, p);
    stash_[stashIndex(part, previous, p)].store(load(index), std::memory_order_relaxed);
    const float value = stash_[stashIndex(part, next, p)].load(std::memory_order_relaxed);
    values_[index].store(value, std::memory_order_relaxed);
    queueHostUpdate({paramSpec(index).id, value});
  }
  currentEngine_[size_t(part)] = next;
  engineChanged_.store(true, std::memory_order_release);
}

void Synth::queueHostUpdate(const HostUpdate& update) {
  for (int i = 0; i < hostUpdateCount_; ++i) {  // a later switch replaces an untaken value
    if (hostUpdates_[i].id == update.id) {
      hostUpdates_[i].value = update.value;
      return;
    }
  }
  if (hostUpdateCount_ < int(2 * kSourceParamCount)) hostUpdates_[hostUpdateCount_++] = update;
}

float Synth::softLimit(float x) {
  const float a = std::fabs(x);
  if (a <= 1.0f) return x;
  const float d = a - 1.0f;
  return std::copysign(1.0f + d / (1.0f + d), x);
}

// External = plugin input; Other part = A takes B's output delayed by exactly
// feedDelay_ samples (whatever the block sizes), B takes A's current chunk; Both =
// their sum. Other-part feeds pass the soft limiter.
void Synth::routeInput(int part, const float* const* in, int n) {
  const int source = std::clamp(parts_[size_t(part)]->settings().inputSource, 0, 2);
  const bool external = (source == 0 || source == 2) && in != nullptr;
  const bool other = source >= 1;
  for (int ch = 0; ch < 2; ++ch) {
    float* dst = partIn_[part][ch].data();
    for (int i = 0; i < n; ++i) {
      float v = external ? in[ch][i] : 0.0f;
      if (other) {
        const float feed = part == 0 ? feedRing_[ch][(feedPos_ + size_t(i)) % feedRing_[ch].size()]
                                     : partOut_[0][ch][size_t(i)];
        v += softLimit(feed);
      }
      dst[i] = v;
    }
  }
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
  s.engine = sourceEngineFromSelector(load(sourceEngineParamIndex(part)));
  s.inputSource = int(v(PartParam::InputSource));
  return s;
}

void Synth::process(const float* const* in, float* const* mainOut, float* const* bus2Out, int n) {
  applyPending();
  for (int p = 0; p < kNumParts; ++p) parts_[size_t(p)]->applySettings(readPartSettings(p));
  masterGain_.setTarget(load(masterParamIndex(MasterParam::Level)));
  dryGain_.setTarget(load(masterParamIndex(MasterParam::Dry)));
  const auto mode = PartMode(int(load(masterParamIndex(MasterParam::Mode))));
  partBMute_.setTarget(mode == PartMode::Single ? 0.0f : 1.0f);

  // Fixed internal chunks keep the B -> A delay at feedDelay_ for any host
  // block size. Each chunk reads its input range before writing the same
  // output range, so in-place buffers stay safe.
  const int chunk = std::max(1, int(feedDelay_));
  for (int done = 0; done < n;) {
    const int count = std::min(chunk, n - done);
    const float* inSlice[2] = {in ? in[0] + done : nullptr, in ? in[1] + done : nullptr};
    float* outSlice[2] = {mainOut[0] + done, mainOut[1] + done};
    float* busSlice[2] = {bus2Out ? bus2Out[0] + done : nullptr, bus2Out ? bus2Out[1] + done : nullptr};
    processChunk(in ? inSlice : nullptr, outSlice, bus2Out ? busSlice : nullptr, count);
    done += count;
  }
}

void Synth::processChunk(const float* const* in, float* const* mainOut, float* const* bus2Out, int n) {
  bool toBus2[kNumParts];
  bool heard[kNumParts];
  for (int p = 0; p < kNumParts; ++p) {  // A before B: A -> B has no delay
    ParamValues source;
    for (int i = 0; i < int(kSourceParamCount); ++i) source.p[i] = load(sourceParamIndex(p, i));
    const int inputSource = parts_[size_t(p)]->settings().inputSource;
    const bool hasInput = inputSource >= 1 || in != nullptr;
    if (hasInput) routeInput(p, in, n);
    const float* partIn[2] = {partIn_[p][0].data(), partIn_[p][1].data()};
    float* out[2] = {partOut_[p][0].data(), partOut_[p][1].data()};
    parts_[size_t(p)]->process(hasInput ? partIn : nullptr, out, source, n);
    const int output = parts_[size_t(p)]->settings().output;
    heard[p] = output != 2;  // Other part only: feeds the other part, not heard
    toBus2[p] = bus2Out != nullptr && output == 1;
  }
  // Append B's chunk to the feed ring. feedPos_ is where A reads next: the
  // sample written feedDelay_ samples before it (ring size 2 * feedDelay_).
  const size_t ringSize = feedRing_[0].size();
  for (int ch = 0; ch < 2; ++ch) {
    for (int i = 0; i < n; ++i) {
      feedRing_[ch][(feedPos_ + feedDelay_ + size_t(i)) % ringSize] = partOut_[1][ch][size_t(i)];
    }
  }
  feedPos_ = (feedPos_ + size_t(n)) % ringSize;

  for (int i = 0; i < n; ++i) {
    float main[2] = {0.0f, 0.0f};
    float bus[2] = {0.0f, 0.0f};
    const float bMute = partBMute_.next();
    for (int p = 0; p < kNumParts; ++p) {
      if (!heard[p]) continue;
      const float mute = p == 0 ? 1.0f : bMute;
      float* target = toBus2[p] ? bus : main;
      target[0] += partOut_[p][0][size_t(i)] * mute;
      target[1] += partOut_[p][1][size_t(i)] * mute;
    }
    const float gain = masterGain_.next();
    const float dry = dryGain_.next();
    // Read both input channels before writing either output (all may alias).
    float input[2] = {0.0f, 0.0f};
    if (in != nullptr) {
      input[0] = in[0][i];
      input[1] = in[1][i];
    }
    float delayed[2] = {input[0], input[1]};
    if (latency_ > 0) {
      for (int ch = 0; ch < 2; ++ch) {
        delayed[ch] = dryDelay_[ch][dryPos_];
        dryDelay_[ch][dryPos_] = input[ch];  // zeros when there is no input
      }
      dryPos_ = (dryPos_ + 1) % size_t(latency_);
    }
    for (int ch = 0; ch < 2; ++ch) {
      mainOut[ch][i] = main[ch] * gain + delayed[ch] * dry;
      if (bus2Out != nullptr) bus2Out[ch][i] = bus[ch] * gain;
    }
  }
}

}  // namespace eg13
