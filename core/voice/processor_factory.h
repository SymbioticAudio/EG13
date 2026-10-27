#pragma once

#include <vector>

#include "voice/source_factory.h"

namespace eg13 {

// Filter and Shaper engines with the rate and block size each runs at (SourceSpec serves
// every slot).
SourceSpec makeOff();
SourceSpec makeFilter(int engine);  // index = FilterEngine; unknown -> Off
SourceSpec makeShaper(int engine);  // index = ShaperEngine; unknown -> Off; all but Off: dcBlock
// Part FX engines (M5): Granular 32000 Hz / 32, Granular Delay host rate (nested converters),
// Resonator 48000 / 24, Reverb 32000 / 16. Each runs in whole native blocks (ruling G8).
SourceSpec makePartFx(int engine);  // index = PartFxEngine; unknown -> Off; all but Off: dcBlock
std::vector<SourceSpec> makeAllPartFx();
std::vector<SourceSpec> makeAllFilters();
std::vector<SourceSpec> makeAllShapers();
std::vector<SourceSpec> makeOffOnly();  // a slot that can only be Off (test voices)

}  // namespace eg13
