#include "mod/mod_factory.h"

#include "engine/engine_registry.h"
#include "engine/marbles_engine.h"
#include "engine/peaks_engine.h"
#include "engine/stages_engine.h"
#include "engine/tides_engine.h"

namespace eg13 {

std::unique_ptr<ModEngine> makeModEngine(int engine) {
  switch (ModEngineKind(engine)) {
    case ModEngineKind::Envelope: return std::make_unique<StagesEngine>();
    case ModEngineKind::Lfo: return std::make_unique<TidesEngine>();
    case ModEngineKind::Random: return std::make_unique<MarblesEngine>();
    case ModEngineKind::FunctionGenerator: return std::make_unique<PeaksEngine>();
    case ModEngineKind::Off: break;
  }
  return std::make_unique<OffModEngine>();
}

std::vector<std::unique_ptr<ModEngine>> makeAllModEngines() {
  std::vector<std::unique_ptr<ModEngine>> all;
  for (int e = 0; e < kNumModEngines; ++e) all.push_back(makeModEngine(e));
  return all;
}

}  // namespace eg13
