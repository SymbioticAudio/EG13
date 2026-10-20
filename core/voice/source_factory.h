#pragma once

#include <memory>

#include "engine/voice_engine.h"

namespace eg13 {

// A source engine plus the native rate and block size it runs at.
struct SourceSpec {
  std::unique_ptr<VoiceEngine> engine;
  double nativeRate;
  int nativeBlock;
};

using SourceFactory = SourceSpec (*)();

SourceSpec makePlaitsSource();

}  // namespace eg13
