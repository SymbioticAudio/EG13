#pragma once

#include <clap/clap.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

#include "synth/synth.h"
#include "util/spsc_queue.h"

namespace eg13 {

// One editor action on its way to the audio thread. (Notes from the editor are M6b.)
struct UiEvent {
  enum class Type : uint8_t { BeginEdit, Edit, EndEdit };
  Type type = Type::Edit;
  uint32_t id = 0;
  double value = 0.0;  // parameter value, in the host's parameter units
};

// Carries knob edits from the editor (main thread) to the synth and the host.
// Edits travel through a lock-free queue and reach the synth (Synth::setParam) and the host
// (CLAP gesture begin, value and gesture end events) in process() or params flush.
// Values are the parameter's own units, the same ones CLAP_EVENT_PARAM_VALUE carries.
// The synth's derived updates (a selector change that swaps P values) still go out through
// the plugin's sendHostUpdates; they never include the edited id, so no value is sent twice.
class UiBridge {
 public:
  static constexpr size_t kQueueSize = 1024;

  // requestFlush runs on the main thread after each queued event: the plugin asks the host
  // for a params flush when it is not processing.
  explicit UiBridge(Synth& synth, std::function<void()> requestFlush);
  // Closes any gesture still open (closeAllGestures). The end events only reach the host if a
  // drain follows, so the owner closes gestures and flushes before it destroys the bridge.
  ~UiBridge();
  UiBridge(const UiBridge&) = delete;
  UiBridge& operator=(const UiBridge&) = delete;

  // Main thread. Never block: events that do not fit the queue wait in order on the main
  // thread and are retried by the next call or pump().
  void beginEdit(uint32_t id);
  void edit(uint32_t id, double v);
  void endEdit(uint32_t id);
  void pump();
  void closeAllGestures();
  size_t overflowSize() const { return overflow_.size(); }

  // Audio thread in process(); in params flush, whichever thread flush runs on. Never
  // allocates, locks or waits. Applies queued edits to the synth and reports them to out
  // (may be null) in queue order.
  void drain(const clap_output_events_t* out);

 private:
  void push(const UiEvent& event);

  Synth& synth_;
  std::function<void()> requestFlush_;
  SpscQueue<UiEvent, kQueueSize> queue_;
  std::deque<UiEvent> overflow_;      // main thread only
  std::vector<uint32_t> openGestures_;  // main thread only
};

}  // namespace eg13
