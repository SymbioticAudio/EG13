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
};

using SourceFactory = SourceSpec (*)();

SourceSpec makePlaitsSource();

SourceSpec makeSource(int engine);  // index = SourceEngine; unknown -> Macro
std::vector<SourceSpec> makeAllSources();

}  // namespace eg13
