#include "voice/source_factory.h"

#include "engine/plaits_engine.h"

namespace eg13 {

SourceSpec makePlaitsSource() {
  return {std::make_unique<PlaitsEngine>(), PlaitsEngine::kNativeRate, 12};
}

}  // namespace eg13
