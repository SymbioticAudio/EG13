#pragma once

#include "engine/voice_engine.h"

namespace eg13 {

// Source engine ids. Saved in projects: never renumber.
enum class SourceEngine : int { Macro = 0, Digital = 1, Modal = 2, Resonator = 3, AudioInput = 4 };
constexpr int kNumSourceEngines = 5;
constexpr int kSourceEngineSelectorMax = 31;  // fixed host range so adding engines changes nothing

int sourceEngineFromSelector(double value);  // unknown values -> Macro
const EngineInfo& sourceEngineInfo(int engine);

}  // namespace eg13
