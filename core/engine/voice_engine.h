#pragma once

#include <cstddef>

namespace eg13 {

constexpr int kMaxSlotParams = 8;

// Post-modulation slot parameter values, normalized 0..1.
struct ParamValues {
  float p[kMaxSlotParams] = {};
};

using ParamFormatter = void (*)(float normalized, char* buf, size_t size);

struct EngineParamInfo {
  const char* name;
  float defaultValue;
  ParamFormatter format;
};

struct EngineInfo {
  const char* id;           // stable, saved in state
  const char* displayName;  // shown in the UI
  int paramCount;
  const EngineParamInfo* params;
};

// Per-voice sound generator or processor (Source, Filter, Shaper slots).
class VoiceEngine {
 public:
  virtual ~VoiceEngine() = default;
  // Main thread, never concurrently with process(). The only place that may allocate.
  virtual void prepare(double hostRate, int maxBlock) = 0;
  virtual void reset() = 0;
  virtual void noteOn(float note, float velocity) = 0;  // note: float MIDI note
  virtual void noteOff() = 0;
  virtual void setNote(float note) = 0;  // pitch change without a new note (glide, bend)
  // out[0], out[1]: n samples each. in may be nullptr.
  virtual void process(const float* const* in, float* const* out,
                       const ParamValues& p, int n) = 0;
  virtual const EngineInfo& info() const = 0;
};

inline float clamp01(float x) {
  if (!(x > 0.0f)) return 0.0f;  // also maps NaN to 0
  return x > 1.0f ? 1.0f : x;
}

}  // namespace eg13
