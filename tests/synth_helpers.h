#pragma once

#include "params/param_ids.h"
#include "synth/param_table.h"
#include "synth/synth.h"

namespace eg13::test {

// For Synth tests written before modulation (M1-M3): the voice as M3 played it. No mod slot, no
// route (the Amp then follows the gate with its 5 ms ramps), Voice Gain 0 dB, Soft Clip off, and
// in the instrument the Filter Off (EG13 FX keeps its Analog Filter, as in M3), and Part FX Off
// (EG13 FX's init patch has Granular in Part A, M5). Main thread.
inline void m3Patch(Synth& synth) {
  for (int part = 0; part < kNumParts; ++part) {
    if (synth.kind() == PluginKind::Instrument) synth.setParam(paramSpec(filterEngineParamIndex(part)).id, 0.0);
    synth.setParam(paramSpec(partFxEngineParamIndex(part)).id, 0.0);
    for (int slot = 0; slot < kNumModSlots; ++slot) synth.setParam(paramSpec(modEngineParamIndex(part, slot)).id, 0.0);
    for (int r = 0; r < kNumRoutes; ++r) synth.setRoute(part, r, Route{});
  }
  synth.setParam(paramSpec(masterParamIndex(MasterParam::VoiceGain)).id, 1.0);
  synth.setParam(paramSpec(masterParamIndex(MasterParam::SoftClip)).id, 0.0);
}

}  // namespace eg13::test
