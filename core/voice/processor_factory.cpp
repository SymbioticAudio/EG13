#include "voice/processor_factory.h"

#include <memory>

#include "engine/beads_engine.h"
#include "engine/bypass_engine.h"
#include "engine/clouds_engine.h"
#include "engine/engine_registry.h"
#include "engine/reverb_engine.h"
#include "engine/rings_fx_engine.h"
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

namespace {

SourceSpec shaperEngine(int engine) {
  switch (ShaperEngine(engine)) {
    case ShaperEngine::RingMod:
      return {std::make_unique<WarpsEngine>(), WarpsEngine::kNativeRate, WarpsEngine::kNativeBlock};
    case ShaperEngine::Eq: return {std::make_unique<ShelvesEngine>(), 0.0, kHostBlock};
    case ShaperEngine::Off: break;
  }
  return makeOff();
}

}  // namespace

SourceSpec makeShaper(int engine) {
  SourceSpec spec = shaperEngine(engine);
  // The DC filter after every Shaper but Off (task 8g): the Ring Mod's fold and ring
  // modulation put out DC (up to -13 dBFS) that nothing after the Shaper removes.
  spec.dcBlock = ShaperEngine(engine) == ShaperEngine::RingMod || ShaperEngine(engine) == ShaperEngine::Eq;
  return spec;
}

namespace {

SourceSpec partFxEngine(int engine) {
  switch (PartFxEngine(engine)) {
    case PartFxEngine::Granular:
      return {std::make_unique<CloudsEngine>(), CloudsEngine::kNativeRate, CloudsEngine::kNativeBlock, true};
    case PartFxEngine::GranularDelay:  // runs at the host rate; its own wrappers convert inside
      return {std::make_unique<BeadsEngine>(), 0.0, kHostBlock, true};
    case PartFxEngine::Resonator:
      return {std::make_unique<RingsFxEngine>(), RingsFxEngine::kNativeRate, RingsFxEngine::kNativeBlock, true};
    case PartFxEngine::Reverb:
      return {std::make_unique<ReverbEngine>(), ReverbEngine::kNativeRate, ReverbEngine::kNativeBlock, true};
    case PartFxEngine::Off: break;
  }
  SourceSpec off = makeOff();
  off.wholeBlocks = true;  // Off pads to the slot's latency, whole-block FIFO included
  return off;
}

}  // namespace

SourceSpec makePartFx(int engine) {
  SourceSpec spec = partFxEngine(engine);
  spec.outputGain = partFxTrim(engine);  // the engine's trim (G11), applied inside the slot's crossfade
  return spec;
}

std::vector<SourceSpec> makeAllPartFx() {
  std::vector<SourceSpec> all;
  for (int e = 0; e < kNumPartFxEngines; ++e) all.push_back(makePartFx(e));
  return all;
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
