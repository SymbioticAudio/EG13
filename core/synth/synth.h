#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "engine/engine_registry.h"
#include "mod/beat_clock.h"
#include "mod/mod_routes.h"
#include "part/part.h"
#include "synth/param_table.h"
#include "synth/plugin_kind.h"
#include "util/smoothed_value.h"
#include "voice/source_factory.h"

namespace eg13 {

using ParamSnapshot = std::array<float, kParamCount>;

// Every engine's remembered parameters, per part and slot: Source engines' P1..P16 first (M2's
// layout, so stashIndex() is unchanged), then Filter engines' P1..P4, Shaper engines' P1..P12,
// mod engines' P1..P16 per mod slot (M4), then each mod engine's Scope per mod slot (M4).
constexpr size_t kSourceStashSize = size_t(kNumParts) * size_t(kNumSourceEngines) * kSourceParamCount;
constexpr size_t kFilterStashSize = size_t(kNumParts) * size_t(kNumFilterEngines) * kFilterParamCount;
constexpr size_t kShaperStashSize = size_t(kNumParts) * size_t(kNumShaperEngines) * kShaperParamCount;
constexpr size_t kModStashSize =
    size_t(kNumParts) * size_t(kNumModSlots) * size_t(kNumModEngines) * kModSlotParamCount;
constexpr size_t kSlotStashSize = kSourceStashSize + kFilterStashSize + kShaperStashSize + kModStashSize;
constexpr size_t kScopeStashSize = size_t(kNumParts) * size_t(kNumModSlots) * size_t(kNumModEngines);
constexpr size_t kStashSize = kSlotStashSize + kScopeStashSize;
constexpr size_t stashIndex(int part, int engine, int p) {
  return (size_t(part) * size_t(kNumSourceEngines) + size_t(engine)) * kSourceParamCount + size_t(p);
}
constexpr size_t slotStashIndex(SlotKind kind, int part, int engine, int p) {
  return kind == SlotKind::Source ? stashIndex(part, engine, p)
         : kind == SlotKind::Filter
             ? kSourceStashSize +
                   (size_t(part) * size_t(kNumFilterEngines) + size_t(engine)) * kFilterParamCount + size_t(p)
         : kind == SlotKind::Shaper
             ? kSourceStashSize + kFilterStashSize +
                   (size_t(part) * size_t(kNumShaperEngines) + size_t(engine)) * kShaperParamCount + size_t(p)
             : kSourceStashSize + kFilterStashSize + kShaperStashSize +
                   ((size_t(part) * size_t(kNumModSlots) + size_t(modSlotIndex(kind))) * size_t(kNumModEngines) +
                    size_t(engine)) * kModSlotParamCount +
                   size_t(p);
}
// A mod engine's remembered Scope (0 per voice, 1 per part) in one mod slot of one part.
constexpr size_t scopeStashIndex(int part, int slot, int engine) {
  return kSlotStashSize + (size_t(part) * size_t(kNumModSlots) + size_t(slot)) * size_t(kNumModEngines) + size_t(engine);
}
// The layout covers the stash exactly: each slot's block starts where the previous ends, and the
// last entry is the last index.
static_assert(slotStashIndex(SlotKind::Filter, 0, 0, 0) == kSourceStashSize, "Filter block starts after Source");
static_assert(slotStashIndex(SlotKind::Shaper, 0, 0, 0) == kSourceStashSize + kFilterStashSize,
              "Shaper block starts after Filter");
static_assert(slotStashIndex(SlotKind::Mod1, 0, 0, 0) == kSourceStashSize + kFilterStashSize + kShaperStashSize,
              "Mod block starts after Shaper");
static_assert(slotStashIndex(SlotKind::Source, kNumParts - 1, kNumSourceEngines - 1, int(kSourceParamCount) - 1) ==
                  kSourceStashSize - 1, "Source block size");
static_assert(slotStashIndex(SlotKind::Filter, kNumParts - 1, kNumFilterEngines - 1, int(kFilterParamCount) - 1) ==
                  kSourceStashSize + kFilterStashSize - 1, "Filter block size");
static_assert(slotStashIndex(SlotKind::Shaper, kNumParts - 1, kNumShaperEngines - 1, int(kShaperParamCount) - 1) ==
                  kSourceStashSize + kFilterStashSize + kShaperStashSize - 1, "Shaper block size");
static_assert(slotStashIndex(SlotKind::Mod4, kNumParts - 1, kNumModEngines - 1, int(kModSlotParamCount) - 1) ==
                  kSlotStashSize - 1, "Mod block size");
static_assert(scopeStashIndex(kNumParts - 1, kNumModSlots - 1, kNumModEngines - 1) == kStashSize - 1,
              "Scope block size");
using EngineStash = std::array<float, kStashSize>;

// Patch data (spec §7.1): what is saved with a project but is not a host parameter. Each part's
// 12 routes (source, destination, via; the amounts are host parameters) and each mod slot's
// Custom envelope shape.
struct PatchData {
  std::array<std::array<Route, kNumRoutes>, kNumParts> routes{};
  std::array<std::array<EnvelopeShape, kNumModSlots>, kNumParts> shapes{};
};

struct HostUpdate {
  uint32_t id;
  float value;
};
// Host-update queue capacity, coalesced per id: per part every slot P, each mod slot's Scope, and
// the four learnable CC numbers (M4).
constexpr size_t kMaxHostUpdates =
    size_t(kNumParts) * (kSourceParamCount + kFilterParamCount + kShaperParamCount +
                         size_t(kNumModSlots) * (kModSlotParamCount + 1) + 4);

// Two parts, MIDI routing by mode, main and Bus 2 outputs, Dry input and
// atomic parameter snapshots.
class Synth {
 public:
  explicit Synth(PluginKind kind);                // every source engine
  Synth(PluginKind kind, SourceFactory factory);  // one engine per voice (tests)
  Synth(const Synth&) = delete;
  Synth& operator=(const Synth&) = delete;

  // B -> A feed delay in samples (or maxBlock, if shorter), independent of the
  // host's block sizes. process() runs the parts in chunks of at most this.
  static constexpr int kFeedBlock = 64;

  PluginKind kind() const { return kind_; }

  void prepare(double sampleRate, int maxBlock);  // main thread, not processing
  // Main thread, from activate (true, after prepare) and deactivate (false).
  // While inactive no audio thread runs and loadParams applies at once.
  void setActive(bool active);
  // Audio thread: first thing in every process call and params flush. Takes any
  // pending snapshot so later same-block automation is not overwritten.
  void beginBlock();
  void reset();

  void noteOn(int channel, int key, float velocity);
  void noteOff(int channel, int key);  // channel < 0: every channel
  void pitchBend(int channel, float value);
  void controlChange(int channel, int cc, int value);
  void allNotesOff();

  // Audio thread while active, main thread while inactive. Never from both at once.
  bool setParam(uint32_t id, double value);
  // Main thread. Reflects a pending snapshot.
  double param(uint32_t id) const;
  ParamSnapshot defaultSnapshot() const;
  ParamSnapshot snapshot() const;
  // Main thread. Applied whole at the next beginBlock/process when active, with the patch data.
  // A stash replaces every engine's remembered settings; without one they reset. Without patch
  // data the plugin's init patch routes apply (defaultPatch()).
  void loadParams(const ParamSnapshot& values, const EngineStash* stash = nullptr, const PatchData* patch = nullptr);
  void resetParamsToDefaults() { loadParams(defaultSnapshot()); }
  EngineStash engineStash() const;  // main thread; current engines' entries = live P values
  static EngineStash defaultStash();  // each engine's own defaults
  static PatchData defaultPatch(PluginKind kind);  // the init patch's routes; ADSR custom shapes
  // Main thread. The patch data as the main thread last set it (applied to the audio at the next
  // block when active).
  PatchData patch() const { return mainPatch_; }
  // Audio thread, or any thread while inactive (tests): the audio thread's copies of the patch
  // data and of one parameter's value, as applied at the last block.
  const PatchData& appliedPatch() const { return patch_; }
  float appliedValue(size_t index) const { return load(index); }
  void setRoute(int part, int route, const Route& value);
  void setEnvelopeShape(int part, int slot, const EnvelopeShape& shape);
  // Audio thread, at the start of each host block: the host's transport, for tempo sync and the
  // Transport Phase source. Without it the beat clock free-runs at 120 BPM.
  void setTransport(const Transport& transport) { clock_.setTransport(transport); }
  int engine(int part) const { return engine(SlotKind::Source, part); }  // main thread
  int engine(SlotKind kind, int part) const;                               // main thread
  // Main thread. Engine-aware: Source, Filter and Shaper P values follow the part's selected engines.
  const char* paramName(size_t index) const;
  void formatParam(size_t index, double value, char* buf, size_t size) const;

  // Audio thread: P values that engine switches changed, for the host. Copies
  // up to max; the rest stay queued for the next call. With max >=
  // kMaxHostUpdates one call always takes everything. A selector change
  // through setParam switches at once: that slot's P values go to the old
  // engine's stash entry and the new engine's entry is loaded.
  int takeHostUpdates(HostUpdate* out, int max);
  // Main thread: true once after any engine switch (names and text changed).
  bool takeEngineChanged() { return engineChanged_.exchange(false, std::memory_order_acq_rel); }

  // in: nullable, may alias mainOut. bus2Out: nullable. n <= maxBlock.
  // Before prepare it only zeroes the outputs.
  void process(const float* const* in, float* const* mainOut, float* const* bus2Out, int n);

  int latencySamples() const { return latency_; }
  int activeVoiceCount(int part) const { return parts_[size_t(part)]->activeVoiceCount(); }

 private:
  float load(size_t index) const { return values_[index].load(std::memory_order_relaxed); }
  bool hasPending() const {
    const int state = pendingState_.load(std::memory_order_acquire);
    return state == kReady || state == kApplying;
  }
  void store(const ParamSnapshot& values);
  void applyPending();
  void publishPatch();  // main thread: mainPatch_ to the audio thread
  PartSettings readPartSettings(int part) const;
  static int clampChannel(int channel);
  void switchEngine(SlotKind kind, int part, int next);  // from setParam, before storing the selector
  void adoptEngines(const ParamSnapshot& values);        // currentEngine_ from a loaded snapshot
  PartEngines partEngines(int part) const;               // main thread
  void feedOther(int part, int n);  // otherIn_[part]: the other part's output, soft-limited
  void queueHostUpdate(const HostUpdate& update);
  void routeInput(int part, const float* const* in, int n);
  // One chunk of process(): n <= feedDelay_, so A only reads B samples already written.
  void processChunk(const float* const* in, float* const* mainOut, float* const* bus2Out, int n);
  static float softLimit(float x);

  PluginKind kind_;
  std::array<std::unique_ptr<Part>, kNumParts> parts_;
  std::array<std::atomic<float>, kParamCount> values_;
  ParamSnapshot pendingValues_{};
  enum : int { kIdle = 0, kWriting = 1, kReady = 2, kApplying = 3 };
  std::atomic<bool> active_{false};
  std::atomic<int> pendingState_{kIdle};
  SmoothedValue masterGain_;
  SmoothedValue dryGain_;
  SmoothedValue partBMute_;
  int latency_ = 0;
  std::vector<float> dryDelay_[2];
  size_t dryPos_ = 0;
  std::vector<float> partOut_[kNumParts][2];
  uint8_t routed_[16][128] = {};

  std::array<std::atomic<float>, kStashSize> stash_;
  EngineStash pendingStash_{};
  std::array<std::array<int, kNumParts>, kNumSlotKinds> currentEngine_{};  // setParam's thread
  HostUpdate hostUpdates_[kMaxHostUpdates] = {};
  int hostUpdateCount_ = 0;
  std::atomic<bool> engineChanged_{false};
  std::vector<float> partIn_[kNumParts][2];
  std::vector<float> otherIn_[kNumParts][2];  // the other part's output, soft-limited
  std::vector<float> feedRing_[2];  // B's output for A, delayed by feedDelay_
  // Patch data: the audio thread's copy, the main thread's, the copy a load publishes with its
  // values, and a main-thread edit waiting for the audio thread (same states as pendingState_).
  PatchData patch_;
  PatchData mainPatch_;
  PatchData pendingPatch_;
  PatchData editPatch_;
  std::atomic<int> editState_{kIdle};
  BeatClock clock_;
  size_t feedDelay_ = 0;            // min(maxBlock, kFeedBlock)
  size_t feedPos_ = 0;
};

}  // namespace eg13
