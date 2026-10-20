#include "engine/engine_registry.h"

#include <cmath>

#include "engine/audio_input_engine.h"
#include "engine/braids_engine.h"
#include "engine/elements_engine.h"
#include "engine/plaits_engine.h"
#include "engine/rings_engine.h"

namespace eg13 {

int sourceEngineFromSelector(double value) {
  if (!std::isfinite(value)) return 0;
  const long e = std::lround(value);
  return e >= 0 && e < kNumSourceEngines ? int(e) : 0;
}

const EngineInfo& sourceEngineInfo(int engine) {
  switch (SourceEngine(engine)) {
    case SourceEngine::Digital: return BraidsEngine::staticInfo();
    case SourceEngine::Modal: return ElementsEngine::staticInfo();
    case SourceEngine::Resonator: return RingsEngine::staticInfo();
    case SourceEngine::AudioInput: return AudioInputEngine::staticInfo();
    case SourceEngine::Macro: break;
  }
  return PlaitsEngine::staticInfo();
}

}  // namespace eg13
