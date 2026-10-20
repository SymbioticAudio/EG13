#pragma once

#include <clap/clap.h>

#include <cstddef>
#include <cstdint>
#include <deque>

#include "eg13/core_api.h"
#include "synth/synth.h"
#include "util/spsc_queue.h"

namespace eg13 {

// One editor action on its way to the audio thread.
struct UiEvent {
  enum class Type : uint8_t { BeginEdit, Edit, EndEdit, NoteOn, NoteOff };
  Type type = Type::Edit;
  int8_t channel = 0;
  int16_t key = 0;
  uint32_t id = 0;
  double value = 0.0;  // parameter value, or note velocity
};

// Host requests the editor triggers. Main thread.
struct UiHost {
  void* context = nullptr;
  void (*requestFlush)(void* context) = nullptr;
  void (*requestZoom)(void* context, double zoom) = nullptr;
};

// The plugin's implementation of core-api/include/eg13/core_api.h (spec §8 Plumbing).
// Editor edits travel through a lock-free queue and reach the synth and the host in
// process() or params flush, as CLAP parameter events wrapped in gestures.
class UiBridge {
 public:
  static constexpr size_t kQueueSize = 1024;

  UiBridge(Synth& synth, UiHost host);
  UiBridge(const UiBridge&) = delete;
  UiBridge& operator=(const UiBridge&) = delete;

  const EG13CoreAPI* api() const { return &api_; }

  // Main thread. Never blocks: events that do not fit the queue wait on the main
  // thread, in order, and are retried by the next push, pump or snapshot.
  void push(const UiEvent& event);
  void pump();
  size_t overflowSize() const { return overflow_.size(); }

  // Audio thread in process(); in params flush, whichever thread flush runs on.
  // Never allocates, locks or waits. Applies queued edits to the synth and reports
  // them to the host (out may be null). Note-ons are applied only when withNotes
  // (process); note-offs always, so a note never sticks.
  void drain(const clap_output_events_t* out, bool withNotes);

  // Main thread: the C API entry points.
  uint32_t paramCount() const;
  bool paramInfo(uint32_t index, EG13ParamInfo* info) const;
  bool paramName(uint32_t index, char* buffer, uint32_t size) const;
  bool paramText(uint32_t index, double value, char* buffer, uint32_t size) const;
  uint32_t engineCount(uint32_t slot) const;
  bool engineName(uint32_t slot, uint32_t engine, char* buffer, uint32_t size) const;
  bool engineId(uint32_t slot, uint32_t engine, char* buffer, uint32_t size) const;
  void snapshot(EG13Snapshot* out);
  void requestZoom(double zoom);

 private:
  Synth& synth_;
  UiHost host_;
  EG13CoreAPI api_{};
  SpscQueue<UiEvent, kQueueSize> queue_;
  std::deque<UiEvent> overflow_;  // main thread only
};

}  // namespace eg13
