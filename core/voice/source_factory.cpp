#include "voice/source_factory.h"

#include "engine/audio_input_engine.h"
#include "engine/braids_engine.h"
#include "engine/elements_engine.h"
#include "engine/engine_registry.h"
#include "engine/plaits_engine.h"
#include "engine/rings_engine.h"

namespace eg13 {

SourceSpec makePlaitsSource() {
  return {std::make_unique<PlaitsEngine>(), PlaitsEngine::kNativeRate, 12};
}

SourceSpec makeSource(int engine) {
  switch (SourceEngine(engine)) {
    case SourceEngine::Digital:
      return {std::make_unique<BraidsEngine>(), BraidsEngine::kNativeRate, BraidsEngine::kNativeBlock};
    case SourceEngine::Modal:
      return {std::make_unique<ElementsEngine>(), ElementsEngine::kNativeRate,
              ElementsEngine::kNativeBlock};
    case SourceEngine::Resonator:
      return {std::make_unique<RingsEngine>(), RingsEngine::kNativeRate, RingsEngine::kNativeBlock};
    case SourceEngine::AudioInput:
      return {std::make_unique<AudioInputEngine>(), 0.0, 16};  // host rate
    case SourceEngine::Macro:
      break;
  }
  return makePlaitsSource();
}

std::vector<SourceSpec> makeAllSources() {
  std::vector<SourceSpec> sources;
  for (int e = 0; e < kNumSourceEngines; ++e) sources.push_back(makeSource(e));
  return sources;
}

}  // namespace eg13
