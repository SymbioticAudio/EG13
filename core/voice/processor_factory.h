#pragma once

#include <vector>

#include "voice/source_factory.h"

namespace eg13 {

// Filter and Shaper engines with the rate and block size each runs at (SourceSpec serves
// every slot).
SourceSpec makeOff();
SourceSpec makeFilter(int engine);  // index = FilterEngine; unknown -> Off
SourceSpec makeShaper(int engine);  // index = ShaperEngine; unknown -> Off
std::vector<SourceSpec> makeAllFilters();
std::vector<SourceSpec> makeAllShapers();
std::vector<SourceSpec> makeOffOnly();  // a slot that can only be Off (test voices)

}  // namespace eg13
