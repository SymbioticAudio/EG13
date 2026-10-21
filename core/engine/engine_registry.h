#pragma once

#include "engine/voice_engine.h"

namespace eg13 {

// Source engine ids. Saved in projects: never renumber.
enum class SourceEngine : int { Macro = 0, Digital = 1, Modal = 2, Resonator = 3, AudioInput = 4 };
constexpr int kNumSourceEngines = 5;
constexpr int kSourceEngineSelectorMax = 31;  // fixed host range so adding engines changes nothing

int sourceEngineFromSelector(double value);  // unknown values -> Macro
const EngineInfo& sourceEngineInfo(int engine);

// Filter and Shaper engine ids. Saved in projects: never renumber.
enum class FilterEngine : int { Off = 0, Analog = 1, Classic = 2, StateVariable = 3 };
constexpr int kNumFilterEngines = 4;
constexpr int kFilterEngineSelectorMax = 31;

enum class ShaperEngine : int { Off = 0, RingMod = 1, Eq = 2 };
constexpr int kNumShaperEngines = 3;
constexpr int kShaperEngineSelectorMax = 31;

int filterEngineFromSelector(double value);  // unknown values -> Off
int shaperEngineFromSelector(double value);  // unknown values -> Off
const EngineInfo& filterEngineInfo(int engine);  // unknown -> Off's
const EngineInfo& shaperEngineInfo(int engine);  // unknown -> Off's

}  // namespace eg13
