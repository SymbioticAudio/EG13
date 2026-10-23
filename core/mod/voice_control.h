#pragma once

#include "voice/slot_params.h"

namespace eg13 {

// What the matrix gives one voice for one control tick (spec §5.2, §6): its modulated slot
// parameters, pitch, Amp and pan, and the gains of gain staging. The Voice moves the Amp, pan
// and gains in straight lines across the tick.
struct VoiceControl {
  SlotParams params;
  float pitch = 0.0f;       // semitones added to the note: matrix Pitch + per-note bend
  bool ampRouted = false;   // a route targets the Amp: amp is its level; otherwise the gate's
  float amp = 1.0f;         // 0..1, when routed
  float pan = 0.0f;         // -1 (left) .. 1 (right)
  float gain = 1.0f;        // Voice Gain: -12 dB or 0 dB (the Amp's gain)
  float sourceGain = 1.0f;  // the Source's trim x Audio Input chord scaling
};

}  // namespace eg13
