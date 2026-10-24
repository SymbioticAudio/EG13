#pragma once

#include <string>

namespace eg13 {
class Synth;
}

namespace eg13::state {

constexpr int kSchemaVersion = 6;

enum class LoadResult { Ok, OkWithWarnings, Invalid };

// Saves and loads a synth's state as JSON. Keeps data it does not understand
// (unknown parameters and top-level keys, and a newer version's extra routes, route numbers,
// envelope segments and parts) and writes it back on save.
// Main thread only; one store per plugin instance.
class StateStore {
 public:
  std::string save(const Synth& synth) const;
  // Invalid leaves the synth and the preserved data unchanged.
  LoadResult load(Synth& synth, const std::string& json);

 private:
  std::string extraTopLevel_ = "{}";
  std::string extraParams_ = "{}";
  // "routes" and "envelopeShapes" as loaded (empty if absent): what this version cannot hold (a
  // newer version's extra routes, numbers, segments and parts) is written back on save.
  std::string loadedRoutes_;
  std::string loadedShapes_;
};

}  // namespace eg13::state
