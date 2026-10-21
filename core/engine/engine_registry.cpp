#include "engine/engine_registry.h"

#include <cmath>

#include "engine/audio_input_engine.h"
#include "engine/braids_engine.h"
#include "engine/bypass_engine.h"
#include "engine/elements_engine.h"
#include "engine/marbles_engine.h"
#include "engine/peaks_engine.h"
#include "engine/plaits_engine.h"
#include "engine/rings_engine.h"
#include "engine/ripples_engine.h"
#include "engine/shelves_engine.h"
#include "engine/sst_filter_engine.h"
#include "engine/stages_engine.h"
#include "engine/svf_engine.h"
#include "engine/tides_engine.h"
#include "engine/warps_engine.h"

namespace eg13 {

namespace {
// A stepped selector's value -> engine index; NaN, infinities and unknown values -> 0.
int fromSelector(double value, int count) {
  if (!std::isfinite(value)) return 0;
  const long e = std::lround(value);
  return e >= 0 && e < count ? int(e) : 0;
}
}  // namespace

int sourceEngineFromSelector(double value) { return fromSelector(value, kNumSourceEngines); }

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

int filterEngineFromSelector(double value) { return fromSelector(value, kNumFilterEngines); }

int shaperEngineFromSelector(double value) { return fromSelector(value, kNumShaperEngines); }

const EngineInfo& filterEngineInfo(int engine) {
  switch (FilterEngine(engine)) {
    case FilterEngine::Analog: return RipplesEngine::staticInfo();
    case FilterEngine::Classic: return SstFilterEngine::staticInfo();
    case FilterEngine::StateVariable: return SvfEngine::staticInfo();
    case FilterEngine::Off: break;
  }
  return BypassEngine::staticInfo();
}

const EngineInfo& shaperEngineInfo(int engine) {
  switch (ShaperEngine(engine)) {
    case ShaperEngine::RingMod: return WarpsEngine::staticInfo();
    case ShaperEngine::Eq: return ShelvesEngine::staticInfo();
    case ShaperEngine::Off: break;
  }
  return BypassEngine::staticInfo();
}

int modEngineFromSelector(double value) { return fromSelector(value, kNumModEngines); }

const ModEngineInfo& modEngineInfo(int engine) {
  switch (ModEngineKind(engine)) {
    case ModEngineKind::Envelope: return StagesEngine::staticInfo();
    case ModEngineKind::Lfo: return TidesEngine::staticInfo();
    case ModEngineKind::Random: return MarblesEngine::staticInfo();
    case ModEngineKind::FunctionGenerator: return PeaksEngine::staticInfo();
    case ModEngineKind::Off: break;
  }
  return OffModEngine::staticInfo();
}

}  // namespace eg13
