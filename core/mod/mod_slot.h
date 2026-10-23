#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "engine/mod_engine.h"

namespace eg13 {

// One mod slot instance (a part's, or one voice's): every mod engine, preallocated, so changing
// the engine is a pointer swap (spec §2.2). A change of engine is immediate: the new engine
// starts from reset (provisional ruling 34).
class ModSlot {
 public:
  explicit ModSlot(std::vector<std::unique_ptr<ModEngine>> engines);  // index = engine; empty -> Off only

  void prepare(double hostRate);  // main thread: every engine
  void setSeed(uint32_t seed);    // main thread, before reset(): every engine
  void reset();                   // every engine; outputs 0
  void restart();                 // the current engine only (its voice starts from silence)
  void setEngine(int engine);     // audio thread; clamps; a change resets the new engine

  // One tick of n host samples with the current engine. A non-finite output resets the engine
  // and reads 0 for the tick (spec §9).
  void process(const ModContext& ctx, const ParamValues& p, const EnvelopeShape& shape, int n);

  int engine() const { return current_; }
  int engineCount() const { return int(engines_.size()); }
  const ModEngineInfo& info() const { return engines_[size_t(current_)]->info(); }
  const float* outputs() const { return out_; }  // kMaxModOutputs values
  size_t nanResets() const { return nanResets_; }

 private:
  std::vector<std::unique_ptr<ModEngine>> engines_;
  int current_ = 0;
  float out_[kMaxModOutputs] = {};
  size_t nanResets_ = 0;
};

}  // namespace eg13
