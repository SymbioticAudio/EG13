#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include "engine/plaits_engine.h"
#include "synth/param_table.h"
#include "voice/mono_note_stack.h"

namespace eg13 {

enum class PluginKind { Instrument, Effect };

// M0 synth: one Macro Oscillator voice with last-note priority.
// The Effect kind adds its audio input to the output.
class Synth {
 public:
  explicit Synth(PluginKind kind);
  Synth(const Synth&) = delete;
  Synth& operator=(const Synth&) = delete;

  PluginKind kind() const { return kind_; }

  void prepare(double sampleRate, int maxBlock);  // main thread only
  void reset();
  void noteOn(int key, float velocity);
  void noteOff(int key);
  void allNotesOff();

  // Any thread. Clamps to [0, 1]; returns false for unknown IDs or NaN.
  bool setParam(uint32_t id, double value);
  double param(uint32_t id) const;  // 0 for unknown IDs
  void resetParamsToDefaults();

  // out[0], out[1]: n samples each, may be the same buffer. in may be nullptr
  // and may alias out.
  void process(const float* const* in, float* const* out, int n);

 private:
  static constexpr int kChunk = 64;

  float load(size_t index) const { return values_[index].load(std::memory_order_relaxed); }

  PluginKind kind_;
  PlaitsEngine engine_;
  MonoNoteStack notes_;
  std::array<std::atomic<float>, kParamCount> values_;
  float render_[2][kChunk] = {};
  float input_[2][kChunk] = {};
};

}  // namespace eg13
