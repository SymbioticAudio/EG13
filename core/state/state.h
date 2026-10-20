#pragma once

#include <string>

namespace eg13 {
class Synth;
}

namespace eg13::state {

constexpr int kSchemaVersion = 1;

enum class LoadResult { Ok, OkWithWarnings, Invalid };

std::string save(const Synth& synth);

// Invalid leaves the synth unchanged. Otherwise parameters missing from the
// document are reset to their defaults.
LoadResult load(Synth& synth, const std::string& json);

}  // namespace eg13::state
