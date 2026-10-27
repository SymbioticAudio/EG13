#pragma once

#include <memory>
#include <vector>

#include "engine/voice_engine.h"

namespace eg13 {

// A source engine plus the native rate and block size it runs at.
struct SourceSpec {
  std::unique_ptr<VoiceEngine> engine;
  double nativeRate;
  int nativeBlock;
  // Part FX engines: the slot's wrapper runs them in whole native blocks (setWholeBlocks).
  bool wholeBlocks = false;
  // Part FX engines: the slot scales this engine's output by it (its trim, partFxTrim; M5 G11),
  // inside the slot's crossfade, so each engine keeps its own trim through a type change.
  float outputGain = 1.0f;
  // Shaper and Part FX engines (task 8g): the slot runs this engine's output through its own DC
  // filter (DcBlocker), inside the slot's crossfade. Off has none, so it stays bit-exact.
  bool dcBlock = false;
};

using SourceFactory = SourceSpec (*)();

SourceSpec makePlaitsSource();

SourceSpec makeSource(int engine);  // index = SourceEngine; unknown -> Macro
std::vector<SourceSpec> makeAllSources();

}  // namespace eg13
