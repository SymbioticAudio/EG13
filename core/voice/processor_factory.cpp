#include "voice/processor_factory.h"

#include <memory>

#include "engine/bypass_engine.h"
#include "engine/engine_registry.h"
#include "engine/ripples_engine.h"
#include "engine/shelves_engine.h"
#include "engine/sst_filter_engine.h"
#include "engine/svf_engine.h"
#include "engine/warps_engine.h"

namespace eg13 {

namespace {
constexpr int kHostBlock = 16;  // host-rate engines: the wrapper bypasses; any block size works
}  // namespace

SourceSpec makeOff() { return {std::make_unique<BypassEngine>(), 0.0, kHostBlock}; }

SourceSpec makeFilter(int engine) {
  switch (FilterEngine(engine)) {
    case FilterEngine::Analog: return {std::make_unique<RipplesEngine>(), 0.0, kHostBlock};
    case FilterEngine::Classic: return {std::make_unique<SstFilterEngine>(), 0.0, kHostBlock};
    case FilterEngine::StateVariable: return {std::make_unique<SvfEngine>(), 0.0, kHostBlock};
    case FilterEngine::Off: break;
  }
  return makeOff();
}

SourceSpec makeShaper(int engine) {
  switch (ShaperEngine(engine)) {
    case ShaperEngine::RingMod:
      return {std::make_unique<WarpsEngine>(), WarpsEngine::kNativeRate, WarpsEngine::kNativeBlock};
    case ShaperEngine::Eq: return {std::make_unique<ShelvesEngine>(), 0.0, kHostBlock};
    case ShaperEngine::Off: break;
  }
  return makeOff();
}

std::vector<SourceSpec> makeAllFilters() {
  std::vector<SourceSpec> all;
  for (int e = 0; e < kNumFilterEngines; ++e) all.push_back(makeFilter(e));
  return all;
}

std::vector<SourceSpec> makeAllShapers() {
  std::vector<SourceSpec> all;
  for (int e = 0; e < kNumShaperEngines; ++e) all.push_back(makeShaper(e));
  return all;
}

std::vector<SourceSpec> makeOffOnly() {
  std::vector<SourceSpec> only;
  only.push_back(makeOff());
  return only;
}

}  // namespace eg13
