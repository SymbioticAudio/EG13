#include "synth/synth.h"

#include <algorithm>
#include <cmath>
#include <thread>

#include "util/soft_clip.h"

namespace eg13 {

namespace {
constexpr double kGainSmoothingSeconds = 0.01;
constexpr float kVoiceHeadroom = 0.25118864f;  // -12 dB (Voice Gain option 0, provisional ruling 24)
}  // namespace

Synth::Synth(PluginKind kind) : kind_(kind) {
  for (auto& learn : learn_) learn.store(-1, std::memory_order_relaxed);
  for (auto& part : parts_) part = std::make_unique<Part>();
  seedParts();
  loadParams(defaultSnapshot());
}

Synth::Synth(PluginKind kind, SourceFactory factory) : kind_(kind) {
  for (auto& learn : learn_) learn.store(-1, std::memory_order_relaxed);
  for (auto& part : parts_) part = std::make_unique<Part>(factory);
  seedParts();
  loadParams(defaultSnapshot());
}

// Each part its own random seeds (Random / S&H, the Random source). Constructors, before prepare().
void Synth::seedParts() {
  for (int p = 0; p < kNumParts; ++p) parts_[size_t(p)]->setSeedBase(uint32_t(p + 1) * 1000u);
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
  for (auto& part : otherIn_) {
    for (auto& buffer : part) buffer.assign(size_t(maxBlock), 0.0f);
  }
  feedDelay_ = size_t(std::min(maxBlock, kFeedBlock));
  for (auto& ring : feedRing_) ring.assign(2 * feedDelay_, 0.0f);
  feedPos_ = 0;
  masterGain_.prepare(sampleRate, kGainSmoothingSeconds);
  dryGain_.prepare(sampleRate, kGainSmoothingSeconds);
  partBMute_.prepare(sampleRate, kGainSmoothingSeconds);
  clock_.prepare(sampleRate);
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
  clock_.reset();
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
    part.noteOn(key, velocity, channel);
    mask = uint8_t(mask | (1u << p));
  }
  routed_[channel][key] = uint8_t(routed_[channel][key] | mask);
}

void Synth::noteOff(int channel, int key, float releaseVelocity) {
  if (key < 0 || key > 127) return;
  const int first = channel < 0 ? 0 : clampChannel(channel);
  const int last = channel < 0 ? 15 : first;
  for (int ch = first; ch <= last; ++ch) {
    const uint8_t mask = routed_[ch][key];
    for (int p = 0; p < kNumParts; ++p) {
      if (mask & (1u << p)) parts_[size_t(p)]->noteOff(key, releaseVelocity);
    }
    routed_[ch][key] = 0;
  }
}

void Synth::pitchBend(int channel, float value) {
  for (auto& part : parts_) {
    const PartSettings& s = part->settings();
    if (s.mpe != 0 && channel >= 0 && channel != s.mpeMaster) {
      if (part->isMemberChannel(channel)) {
        part->memberExpression(channel, Expression::Bend, std::clamp(value, -1.0f, 1.0f) * float(s.mpeBendRange));
      }
    } else if (part->acceptsChannel(channel)) {
      part->pitchBend(value);
    }
  }
}

void Synth::channelPressure(int channel, float value) {
  for (auto& part : parts_) {
    if (part->isMemberChannel(channel)) {
      part->memberExpression(channel, Expression::Pressure, value);
    } else if (part->acceptsChannel(channel)) {
      part->setController(PartController::ChannelPressure, value);
    }
  }
}

void Synth::polyPressure(int channel, int key, float value) {
  for (auto& part : parts_) {
    if (part->acceptsChannel(channel)) part->noteExpression(channel, key, Expression::Pressure, value);
  }
}

void Synth::noteExpression(int channel, int key, Expression expression, float value) {
  for (auto& part : parts_) {
    if (part->acceptsChannel(channel)) part->noteExpression(channel, key, expression, value);
  }
}

// Controllers a learn never takes: sustain, bank select, data entry, data increment and
// decrement, (N)RPN and channel mode.
static bool learnable(int cc) {
  return cc != 0 && cc != 32 && cc != 6 && cc != 38 && cc != 64 && !(cc >= 96 && cc <= 101) && cc < 120;
}

void Synth::controlChange(int channel, int cc, int value) {
  const float v = float(std::clamp(value, 0, 127)) / 127.0f;
  for (int p = 0; p < kNumParts; ++p) {
    Part& part = *parts_[size_t(p)];
    if (!part.acceptsChannel(channel)) continue;
    int learning = learn_[size_t(p)].load(std::memory_order_acquire);
    if (learning >= 0 && learning < 4 && !part.isMemberChannel(channel) && learnable(cc) &&
        learn_[size_t(p)].compare_exchange_strong(learning, -1, std::memory_order_acq_rel)) {
      const size_t index = partParamIndex(p, PartParam(uint32_t(PartParam::CcA) + uint32_t(learning)));
      setParam(paramSpec(index).id, double(cc));
      queueHostUpdate({paramSpec(index).id, float(cc)});
      engineChanged_.store(true, std::memory_order_release);
    }
    const bool member = part.isMemberChannel(channel);
    if (cc == 74) {
      if (member) {
        part.memberExpression(channel, Expression::Slide, v);
      } else {
        part.setController(PartController::Slide, v);
      }
    }
    if (member) continue;  // other controllers on a member channel are not per note: ignored
    if (cc == 1) part.setController(PartController::ModWheel, v);
    for (int k = 0; k < 4; ++k) {
      if (cc == int(load(partParamIndex(p, PartParam(uint32_t(PartParam::CcA) + uint32_t(k)))))) {
        part.setController(PartController(int(PartController::CcA) + k), v);
      }
    }
  }
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
  // Switch at the selector event, not at the next process: P1..P16 events after
  // it in the same block (or flush) then land on the new engine. The P values
  // are swapped before the selector is stored, so a main-thread snapshot() or
  // engineStash() never sees the new selector with the old engine's values.
  // While the slot's P stores are in flight it can still see the old selector
  // with some new P values; that read is benign (display, or a state save the
  // host repeats) and the next one is consistent. Filter and Shaper selectors
  // swap the same way as the Source's (their slots rely on it: an engine
  // waiting for a crossfade keeps its own settings meanwhile).
  SlotKind kind = SlotKind::Source;
  int part = 0;
  if (slotSelectorOf(size_t(index), &kind, &part)) {
    switchEngine(kind, part, slotEngineFromSelector(kind, clamped));
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
  for (int k = 0; k < kNumSlotKinds; ++k) {
    const SlotKind kind = SlotKind(k);
    for (int part = 0; part < kNumParts; ++part) {
      for (int e = 0; e < slotEngineCount(kind); ++e) {
        const EngineInfo& info = slotEngineInfo(kind, e);
        for (int p = 0; p < int(slotParamCount(kind)); ++p) {
          stash[slotStashIndex(kind, part, e, p)] = p < info.paramCount ? info.params[p].defaultValue : 0.0f;
        }
      }
    }
  }
  // Each mod engine's own scope (spec §4: Random / S&H per part, the others per voice).
  for (int part = 0; part < kNumParts; ++part) {
    for (int slot = 0; slot < kNumModSlots; ++slot) {
      for (int e = 0; e < kNumModEngines; ++e) {
        stash[scopeStashIndex(part, slot, e)] = modEngineInfo(e).perPartByDefault ? 1.0f : 0.0f;
      }
    }
  }
  return stash;
}

void Synth::adoptEngines(const ParamSnapshot& values) {
  for (int k = 0; k < kNumSlotKinds; ++k) {
    const SlotKind kind = SlotKind(k);
    for (int part = 0; part < kNumParts; ++part) {
      currentEngine_[size_t(k)][size_t(part)] = slotEngineFromSelector(kind, values[slotEngineParamIndex(kind, part)]);
    }
  }
}

PatchData Synth::defaultPatch(PluginKind kind) {
  PatchData patch;
  for (int part = 0; part < kNumParts; ++part) {
    for (int r = 0; r < kNumRoutes; ++r) patch.routes[size_t(part)][size_t(r)] = initPatchRoute(kind, part, r);
  }
  return patch;  // envelope shapes: EnvelopeShape's default, an ADSR
}

void Synth::loadParams(const ParamSnapshot& values, const EngineStash* stash, const PatchData* patch) {  // main thread
  ParamSnapshot clamped{};
  for (size_t i = 0; i < kParamCount; ++i) clamped[i] = clampParam(i, double(values[i]));
  const EngineStash s = stash ? *stash : defaultStash();
  mainPatch_ = patch ? *patch : defaultPatch(kind_);
  if (!active_.load(std::memory_order_acquire)) {  // no audio thread can run
    store(clamped);
    for (size_t i = 0; i < kStashSize; ++i) stash_[i].store(s[i], std::memory_order_relaxed);
    hostUpdateCount_ = 0;  // superseded by the load
    adoptEngines(clamped);
    patch_ = mainPatch_;
    pendingState_.store(kIdle, std::memory_order_release);
    editState_.store(kIdle, std::memory_order_release);
    return;
  }
  // A route edit still waiting is older than this load: drop it (the load carries mainPatch_).
  for (;;) {
    int expected = kReady;
    if (editState_.compare_exchange_strong(expected, kIdle, std::memory_order_acq_rel)) break;
    if (expected == kIdle) break;
    std::this_thread::yield();  // kApplying: the audio thread is copying the edit
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
  pendingPatch_ = mainPatch_;
  pendingState_.store(kReady, std::memory_order_release);
}

void Synth::setRoute(int part, int route, const Route& value) {  // main thread
  if (part < 0 || part >= kNumParts || route < 0 || route >= kNumRoutes) return;
  mainPatch_.routes[size_t(part)][size_t(route)] = value;
  publishPatch();
  engineChanged_.store(true, std::memory_order_release);  // the route amount's text follows it
}

void Synth::setEnvelopeShape(int part, int slot, const EnvelopeShape& shape) {  // main thread
  if (part < 0 || part >= kNumParts || slot < 0 || slot >= kNumModSlots) return;
  mainPatch_.shapes[size_t(part)][size_t(slot)] = sanitized(shape);
  publishPatch();
}

// The pendingState_ protocol for patch edits: the audio thread takes the whole patch at its next
// block; a newer edit replaces one it has not taken yet.
void Synth::publishPatch() {
  if (!active_.load(std::memory_order_acquire)) {
    patch_ = mainPatch_;
    return;
  }
  for (;;) {
    int expected = kReady;
    if (editState_.compare_exchange_strong(expected, kWriting, std::memory_order_acq_rel)) break;
    expected = kIdle;
    if (editState_.compare_exchange_strong(expected, kWriting, std::memory_order_acq_rel)) break;
    std::this_thread::yield();
  }
  editPatch_ = mainPatch_;
  editState_.store(kReady, std::memory_order_release);
}

// A waiting route edit is claimed before the load is looked at, and applied after it. While the
// edit is claimed (kApplying), loadParams spins in its cancel loop and cannot publish, so a load
// found waiting here was published before the claim; that load did not cancel this edit, so the
// edit is the newer of the two and goes on top of it. A load published after the claim waits for
// the next block whole (its patch with its values). Checking the load first would let an edit
// published after the check carry a newer load's routes one block before that load's values, and
// the load would then overwrite the edit at the next block.
void Synth::applyPending() {  // audio thread, or main thread while inactive
  int edit = kReady;
  const bool haveEdit = editState_.compare_exchange_strong(edit, kApplying, std::memory_order_acq_rel);
  int expected = kReady;
  if (pendingState_.compare_exchange_strong(expected, kApplying, std::memory_order_acq_rel)) {
    store(pendingValues_);
    patch_ = pendingPatch_;  // a load's routes and shapes arrive with its values
    for (size_t i = 0; i < kStashSize; ++i) stash_[i].store(pendingStash_[i], std::memory_order_relaxed);
    hostUpdateCount_ = 0;  // switches from before the load are superseded by it
    // The snapshot's P values belong to its engines: adopt them without swapping.
    adoptEngines(pendingValues_);
    pendingState_.store(kIdle, std::memory_order_release);
  }
  if (haveEdit) {  // newer than the load just applied, if any
    patch_ = editPatch_;
    editState_.store(kIdle, std::memory_order_release);
  }
}

EngineStash Synth::engineStash() const {  // main thread
  EngineStash s{};
  const bool pending = hasPending();
  for (size_t i = 0; i < kStashSize; ++i) {
    s[i] = pending ? pendingStash_[i] : stash_[i].load(std::memory_order_relaxed);
  }
  for (int k = 0; k < kNumSlotKinds; ++k) {
    const SlotKind kind = SlotKind(k);
    for (int part = 0; part < kNumParts; ++part) {
      const int e = engine(kind, part);
      for (int p = 0; p < int(slotParamCount(kind)); ++p) {
        s[slotStashIndex(kind, part, e, p)] = float(param(paramSpec(slotParamIndex(kind, part, p)).id));
      }
    }
  }
  for (int part = 0; part < kNumParts; ++part) {
    for (int slot = 0; slot < kNumModSlots; ++slot) {
      const int e = engine(modSlotKind(slot), part);
      s[scopeStashIndex(part, slot, e)] = float(param(paramSpec(modScopeParamIndex(part, slot)).id));
    }
  }
  return s;
}

int Synth::engine(SlotKind kind, int part) const {
  return slotEngineFromSelector(kind, param(paramSpec(slotEngineParamIndex(kind, part)).id));
}

PartEngines Synth::partEngines(int part) const {
  PartEngines e;
  e.source = engine(SlotKind::Source, part);
  e.filter = engine(SlotKind::Filter, part);
  e.shaper = engine(SlotKind::Shaper, part);
  for (int slot = 0; slot < kNumModSlots; ++slot) e.mod[slot] = engine(modSlotKind(slot), part);
  for (int r = 0; r < kNumRoutes; ++r) e.routeDestination[r] = mainPatch_.routes[size_t(part)][size_t(r)].destination;
  return e;
}

const char* Synth::paramName(size_t index) const {
  const int part = index < size_t(kNumParts) * kParamsPerPart ? int(index / kParamsPerPart) : 0;
  return eg13::paramName(index, partEngines(part));
}

void Synth::formatParam(size_t index, double value, char* buf, size_t size) const {
  const int part = index < size_t(kNumParts) * kParamsPerPart ? int(index / kParamsPerPart) : 0;
  eg13::formatParam(index, value, buf, size, partEngines(part));
}

int Synth::takeHostUpdates(HostUpdate* out, int max) {  // audio thread
  const int count = std::clamp(max, 0, hostUpdateCount_);
  for (int i = 0; i < count; ++i) out[i] = hostUpdates_[i];
  for (int i = count; i < hostUpdateCount_; ++i) hostUpdates_[i - count] = hostUpdates_[i];
  hostUpdateCount_ -= count;
  return count;
}

// Same threading as setParam. Saves the old engine's P values, loads the new
// engine's and queues them for the host.
void Synth::switchEngine(SlotKind kind, int part, int next) {
  int& current = currentEngine_[size_t(kind)][size_t(part)];
  const int previous = current;
  if (next == previous) return;
  for (int p = 0; p < int(slotParamCount(kind)); ++p) {
    const size_t index = slotParamIndex(kind, part, p);
    stash_[slotStashIndex(kind, part, previous, p)].store(load(index), std::memory_order_relaxed);
    const float value = stash_[slotStashIndex(kind, part, next, p)].load(std::memory_order_relaxed);
    values_[index].store(value, std::memory_order_relaxed);
    queueHostUpdate({paramSpec(index).id, value});
  }
  if (isModSlot(kind)) {  // a mod engine remembers its scope too
    const int slot = modSlotIndex(kind);
    const size_t index = modScopeParamIndex(part, slot);
    stash_[scopeStashIndex(part, slot, previous)].store(load(index), std::memory_order_relaxed);
    const float value = stash_[scopeStashIndex(part, slot, next)].load(std::memory_order_relaxed);
    values_[index].store(value, std::memory_order_relaxed);
    queueHostUpdate({paramSpec(index).id, value});
  }
  current = next;
  engineChanged_.store(true, std::memory_order_release);
}

void Synth::queueHostUpdate(const HostUpdate& update) {
  for (int i = 0; i < hostUpdateCount_; ++i) {  // a later switch replaces an untaken value
    if (hostUpdates_[i].id == update.id) {
      hostUpdates_[i].value = update.value;
      return;
    }
  }
  if (hostUpdateCount_ < int(kMaxHostUpdates)) hostUpdates_[hostUpdateCount_++] = update;
}

float Synth::softLimit(float x) {
  const float a = std::fabs(x);
  if (a <= 1.0f) return x;
  const float d = a - 1.0f;
  return std::copysign(1.0f + d / (1.0f + d), x);
}

// The other part's output for this chunk, soft-limited (spec §2.3): A takes B's output delayed
// by exactly feedDelay_ samples (whatever the block sizes); B takes A's current chunk.
void Synth::feedOther(int part, int n) {
  for (int ch = 0; ch < 2; ++ch) {
    float* dst = otherIn_[part][ch].data();
    for (int i = 0; i < n; ++i) {
      const float feed = part == 0 ? feedRing_[ch][(feedPos_ + size_t(i)) % feedRing_[ch].size()]
                                   : partOut_[0][ch][size_t(i)];
      dst[i] = softLimit(feed);
    }
  }
}

// The part's Input by its Input source setting: External = plugin input; Other part = the
// soft-limited other-part feed; Both = their sum.
void Synth::routeInput(int part, const float* const* in, int n) {
  const int source = std::clamp(parts_[size_t(part)]->settings().inputSource, 0, 2);
  const bool external = (source == 0 || source == 2) && in != nullptr;
  const bool other = source >= 1;
  for (int ch = 0; ch < 2; ++ch) {
    float* dst = partIn_[part][ch].data();
    const float* feed = otherIn_[part][ch].data();
    for (int i = 0; i < n; ++i) dst[i] = (external ? in[ch][i] : 0.0f) + (other ? feed[i] : 0.0f);
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
  s.filterEngine = slotEngineFromSelector(SlotKind::Filter, load(filterEngineParamIndex(part)));
  s.shaperEngine = slotEngineFromSelector(SlotKind::Shaper, load(shaperEngineParamIndex(part)));
  s.inputSource = int(v(PartParam::InputSource));
  // MPE zones (provisional ruling 11): the lower zone's master is channel 1, the upper zone's 16;
  // a zone has 15 member channels, or 7 each when the other part uses the other zone.
  s.mpe = int(v(PartParam::MpeMode));
  s.mpeBendRange = int(v(PartParam::MpeBendRange));
  // Only a part that plays counts: in Single mode Part B's MPE setting is ignored. Part A wins
  // channels both parts would use (the same zone chosen twice): Part B then hears none.
  const bool otherPlays = part == 1 || PartMode(int(load(masterParamIndex(MasterParam::Mode)))) != PartMode::Single;
  const int other = otherPlays ? int(load(partParamIndex(1 - part, PartParam::MpeMode))) : 0;
  if (part == 1 && s.mpe != 0 && other == s.mpe) {
    s.mpeMaster = -1;
    s.mpeFirst = 1;
    s.mpeLast = 0;
  } else if (s.mpe == 1) {
    s.mpeMaster = 0;
    s.mpeFirst = 1;
    s.mpeLast = other == 2 ? 7 : 15;
  } else if (s.mpe == 2) {
    s.mpeMaster = 15;
    s.mpeFirst = other == 1 ? 8 : 0;
    s.mpeLast = 14;
  }
  for (int slot = 0; slot < kNumModSlots; ++slot) {
    s.modEngine[slot] = slotEngineFromSelector(modSlotKind(slot), load(modEngineParamIndex(part, slot)));
    s.modPerPart[slot] = load(modScopeParamIndex(part, slot)) >= 0.5f;
  }
  return s;
}

void Synth::process(const float* const* in, float* const* mainOut, float* const* bus2Out, int n) {
  if (feedRing_[0].empty() || partOut_[0][0].empty()) {  // not prepared: no buffers yet
    for (int ch = 0; ch < 2; ++ch) {
      std::fill(mainOut[ch], mainOut[ch] + n, 0.0f);
      if (bus2Out != nullptr) std::fill(bus2Out[ch], bus2Out[ch] + n, 0.0f);
    }
    return;
  }
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
    clock_.advance(count);
    done += count;
  }
}

void Synth::processChunk(const float* const* in, float* const* mainOut, float* const* bus2Out, int n) {
  bool toBus2[kNumParts];
  bool heard[kNumParts];
  ModTiming timing;
  timing.origin = clock_.origin();
  timing.elapsed = clock_.elapsed();
  timing.beatsPerSample = clock_.beatsPerSample();
  timing.jump = clock_.jumped();
  timing.barStart = clock_.barStart();
  timing.beatsPerBar = clock_.beatsPerBar();
  const float voiceGain = load(masterParamIndex(MasterParam::VoiceGain)) >= 0.5f ? 1.0f : kVoiceHeadroom;
  for (int p = 0; p < kNumParts; ++p) {  // A before B: A -> B has no delay
    Part& part = *parts_[size_t(p)];
    PartParams params;
    for (int i = 0; i < int(kSourceParamCount); ++i) params.slots.source.p[i] = load(sourceParamIndex(p, i));
    for (int i = 0; i < int(kFilterParamCount); ++i) params.slots.filter.p[i] = load(filterParamIndex(p, i));
    for (int i = 0; i < int(kShaperParamCount); ++i) params.slots.shaper.p[i] = load(shaperParamIndex(p, i));
    for (int slot = 0; slot < kNumModSlots; ++slot) {
      for (int i = 0; i < int(kModSlotParamCount); ++i) params.mod.slot[slot].p[i] = load(modParamIndex(p, slot, i));
      params.mod.shapes[slot] = patch_.shapes[size_t(p)][size_t(slot)];
    }
    for (int r = 0; r < kNumRoutes; ++r) {
      params.mod.routes.route[r] = patch_.routes[size_t(p)][size_t(r)];
      params.mod.routes.amount[r] = load(routeAmountParamIndex(p, r));
    }
    params.timing = timing;
    params.voiceGain = voiceGain;
    feedOther(p, n);
    const bool hasInput = part.settings().inputSource >= 1 || in != nullptr;
    if (hasInput) routeInput(p, in, n);
    const float* partIn[2] = {partIn_[p][0].data(), partIn_[p][1].data()};
    const float* other[2] = {otherIn_[p][0].data(), otherIn_[p][1].data()};
    float* out[2] = {partOut_[p][0].data(), partOut_[p][1].data()};
    VoiceInputs inputs;
    inputs.input = hasInput ? partIn : nullptr;
    inputs.other = other;
    part.process(inputs, out, params, n);
    const int output = part.settings().output;
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

  const bool softClipOn = load(masterParamIndex(MasterParam::SoftClip)) >= 0.5f;
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
      const float m = main[ch] * gain + delayed[ch] * dry;
      const float b = bus[ch] * gain;
      // The master soft clipper (owner decision 2), on both outputs after Dry.
      mainOut[ch][i] = softClipOn ? softClip(m) : m;
      if (bus2Out != nullptr) bus2Out[ch][i] = softClipOn ? softClip(b) : b;
    }
  }
}

}  // namespace eg13
