#pragma once

#include "engine/voice_engine.h"

namespace eg13 {

// Post-modulation parameters of each slot in the voice chain.
struct SlotParams {
  ParamValues source;
  ParamValues filter;
  ParamValues shaper;
};

}  // namespace eg13
