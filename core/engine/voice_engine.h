#pragma once

#include <algorithm>
#include <cstddef>

namespace eg13 {

constexpr int kMaxSlotParams = 16;  // engines with fewer ignore the extra slots

// Post-modulation slot parameter values, normalized 0..1.
struct ParamValues {
  float p[kMaxSlotParams] = {};
};

using ParamFormatter = void (*)(float normalized, char* buf, size_t size);

// Where a side input of a Filter/Shaper engine (the Ring Mod / Vocoder's CARRIER and MODULATOR)
// takes its signal from. Voice: the engine uses its own input, the voice. None: the engine makes
// the signal itself (Warps' internal oscillator). The Voice passes nullptr for both.
enum class SideSource : int { Voice = 0, SourceAux = 1, Input = 2, OtherPart = 3, None = 4 };

// For side input `input` (0-based), the source the engine's parameters select.
using SideSourceFn = SideSource (*)(int input, const ParamValues& p);

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
  bool usesInput;           // the engine reads in[0], in[1] of process()
  bool hasAux = false;      // Source: also writes out[2], the module's AUX output, once enabled
  int sideInputs = 0;       // Filter/Shaper: also reads in[2] .. in[1 + sideInputs] (each nullable), <= 2
  SideSourceFn sideSource = nullptr;  // which signal each side input takes
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
  // out[0], out[1]: n samples each (and out[2] for an enabled AUX), n <= the maxBlock given to
  // prepare(). in may be nullptr; in[2] .. in[1 + sideInputs] exist for engines with side inputs
  // and may each be nullptr.
  virtual void process(const float* const* in, float* const* out, const ParamValues& p,
                       int n) = 0;
  virtual const EngineInfo& info() const = 0;
  // Filter/Shaper (spec §5.1): run the right-channel instance only while the input is stereo.
  // Called on the audio thread, before process(), so it must not allocate (prepare() reserves
  // what the right instance needs); engines without a second instance ignore it. An engine inside
  // a NativeRateWrapper receives it when the host block arrives, so it sees the change about one
  // input latency (inputLatencySamples()) before the samples that caused it.
  virtual void setStereo(bool) {}
  // Sources with info().hasAux: write out[2] from now on. Main thread, before prepare(); off by
  // default so two-channel callers stay valid.
  virtual void setAuxEnabled(bool) {}
  // Latency the engine itself adds (e.g. a fixed-block FIFO), in samples at its native rate.
  virtual int nativeLatency() const { return 0; }
};

// For engines whose scratch buffers hold prepare()'s maxBlock: a call longer than `capacity`
// (which breaks the process() contract) runs as pieces of at most `capacity` samples through
// `process(in, out, n)` instead of overrunning, also in release builds. Returns false, doing
// nothing, when n fits. With no capacity (not prepared) it writes silence.
template <typename Process>
bool processInPieces(const float* const* in, float* const* out, int n, int capacity, Process&& process) {
  if (n <= capacity) return false;
  if (capacity <= 0) {
    std::fill(out[0], out[0] + n, 0.0f);
    std::fill(out[1], out[1] + n, 0.0f);
    return true;
  }
  for (int done = 0; done < n; done += capacity) {
    const float* pieceIn[2] = {in != nullptr ? in[0] + done : nullptr, in != nullptr ? in[1] + done : nullptr};
    float* pieceOut[2] = {out[0] + done, out[1] + done};
    process(in != nullptr ? pieceIn : nullptr, pieceOut, std::min(capacity, n - done));
  }
  return true;
}

inline float clamp01(float x) {
  if (!(x > 0.0f)) return 0.0f;  // also maps NaN to 0
  return x > 1.0f ? 1.0f : x;
}

}  // namespace eg13
