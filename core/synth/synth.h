#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "part/part.h"
#include "synth/param_table.h"
#include "synth/plugin_kind.h"
#include "util/smoothed_value.h"
#include "voice/source_factory.h"

namespace eg13 {

using ParamSnapshot = std::array<float, kParamCount>;

// Two parts, MIDI routing by mode, main and Bus 2 outputs, Dry input and
// atomic parameter snapshots.
class Synth {
 public:
  explicit Synth(PluginKind kind, SourceFactory factory = makePlaitsSource);
  Synth(const Synth&) = delete;
  Synth& operator=(const Synth&) = delete;

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
  // Main thread. Applied whole at the next beginBlock/process when active.
  void loadParams(const ParamSnapshot& values);
  void resetParamsToDefaults() { loadParams(defaultSnapshot()); }

  // in: nullable, may alias mainOut. bus2Out: nullable. n <= maxBlock.
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
  PartSettings readPartSettings(int part) const;
  static int clampChannel(int channel);

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
};

}  // namespace eg13
