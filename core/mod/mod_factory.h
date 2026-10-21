#pragma once

#include <memory>
#include <vector>

#include "engine/mod_engine.h"

namespace eg13 {

// Every mod engine of one slot instance, index = ModEngineKind.
using ModEngineFactory = std::vector<std::unique_ptr<ModEngine>> (*)();

std::unique_ptr<ModEngine> makeModEngine(int engine);  // index = ModEngineKind; unknown -> Off
std::vector<std::unique_ptr<ModEngine>> makeAllModEngines();

}  // namespace eg13
