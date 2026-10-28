#pragma once

#include <mutex>
#include <string>
#include <utility>

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

  // The UI's browser state: an opaque string it owns (loaded preset names, per-part edited marks), saved as
  // the "browser" key and restored by load; empty when the state has none.
  // Guarded by a small mutex (never taken on the audio thread): a host may save from another thread
  // (an AU's ClassInfo) while the editor, on the main thread, sets it.
  std::string browser() const {
    const std::lock_guard<std::mutex> lock(browserMutex_);
    return browser_;
  }
  void setBrowser(std::string value) {
    const std::lock_guard<std::mutex> lock(browserMutex_);
    browser_ = std::move(value);
  }

 private:
  std::string extraTopLevel_ = "{}";
  std::string extraParams_ = "{}";
  // "routes" and "envelopeShapes" as loaded (empty if absent): what this version cannot hold (a
  // newer version's extra routes, numbers, segments and parts) is written back on save.
  std::string loadedRoutes_;
  std::string loadedShapes_;
  mutable std::mutex browserMutex_;
  std::string browser_;
};

}  // namespace eg13::state
