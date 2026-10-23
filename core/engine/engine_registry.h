#pragma once

#include "engine/mod_engine.h"
#include "engine/voice_engine.h"

namespace eg13 {

// Source engine ids. Saved in projects: never renumber.
enum class SourceEngine : int { Macro = 0, Digital = 1, Modal = 2, Resonator = 3, AudioInput = 4 };
constexpr int kNumSourceEngines = 5;
constexpr int kSourceEngineSelectorMax = 31;  // fixed host range so adding engines changes nothing

int sourceEngineFromSelector(double value);  // unknown values -> Macro
const EngineInfo& sourceEngineInfo(int engine);

// Gain staging (M4, provisional ruling 25): each Source's trim at its output, before the Filter,
// so a single held note at the engine's defaults peaks near the Macro Oscillator's (planning
// measurement at 48 kHz, velocity 1, C3: peaks 0.66, 0.97, 0.17, 0.17; Audio Input passes its
// input). In dB and as a gain; unknown engines 0 dB. A trim is constant per engine, so it needs
// no delay ring: it sits at the Source output, ahead of the Task 10 latency alignment.
float sourceTrimDb(int engine);
float sourceTrim(int engine);

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

// Part FX engine ids. Saved in projects: never renumber; new engines are appended. Selector values
// without an engine fall back to Off.
enum class PartFxEngine : int { Off = 0, Granular = 1, GranularDelay = 2, Resonator = 3, Reverb = 4 };
constexpr int kNumPartFxEngines = 5;
constexpr int kPartFxEngineSelectorMax = 31;  // fixed host range so adding engines changes nothing

int partFxEngineFromSelector(double value);  // unknown values -> Off
const EngineInfo& partFxEngineInfo(int engine);  // unknown -> Off's

// Gain staging (M5, preflight ruling G11), on the sourceTrimDb precedent: each Part FX engine's
// trim at the slot output, outside the fidelity boundary. Unity (0 dB) until Task 14 measures
// and sets them; unknown engines 0 dB.
float partFxTrimDb(int engine);
float partFxTrim(int engine);

// Mod engine ids. Saved in projects: never renumber; new engines are appended.
enum class ModEngineKind : int { Off = 0, Envelope = 1, Lfo = 2, Random = 3, FunctionGenerator = 4 };
constexpr int kNumModEngines = 5;
constexpr int kModEngineSelectorMax = 31;  // fixed host range so adding engines changes nothing

int modEngineFromSelector(double value);         // unknown values -> Off
const ModEngineInfo& modEngineInfo(int engine);  // unknown -> Off's

}  // namespace eg13
