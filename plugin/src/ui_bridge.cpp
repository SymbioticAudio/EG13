#include "ui_bridge.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "synth/param_table.h"

namespace eg13 {

namespace {

bool pushGesture(const clap_output_events_t* out, uint32_t id, bool begin) {
  clap_event_param_gesture_t ev{};
  ev.header.size = sizeof(ev);
  ev.header.time = 0;
  ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
  ev.header.type = begin ? CLAP_EVENT_PARAM_GESTURE_BEGIN : CLAP_EVENT_PARAM_GESTURE_END;
  ev.header.flags = 0;
  ev.param_id = id;
  return out->try_push(out, &ev.header);
}

bool pushValue(const clap_output_events_t* out, uint32_t id, double value) {
  clap_event_param_value_t ev{};
  ev.header.size = sizeof(ev);
  ev.header.time = 0;
  ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
  ev.header.type = CLAP_EVENT_PARAM_VALUE;
  ev.header.flags = 0;
  ev.param_id = id;
  ev.cookie = nullptr;
  ev.note_id = -1;
  ev.port_index = -1;
  ev.channel = -1;
  ev.key = -1;
  ev.value = value;
  return out->try_push(out, &ev.header);
}

}  // namespace

UiBridge::UiBridge(Synth& synth, std::function<void()> requestFlush,
                   std::function<void()> requestPump)
    : synth_(synth), requestFlush_(std::move(requestFlush)), requestPump_(std::move(requestPump)) {}

UiBridge::~UiBridge() { closeAllGestures(); }

void UiBridge::beginEdit(uint32_t id) {
  if (std::find(openGestures_.begin(), openGestures_.end(), id) != openGestures_.end()) return;
  openGestures_.push_back(id);
  push({UiEvent::Type::BeginEdit, id, 0.0});
}

void UiBridge::edit(uint32_t id, double v) { push({UiEvent::Type::Edit, id, v}); }

void UiBridge::endEdit(uint32_t id) {
  const auto it = std::find(openGestures_.begin(), openGestures_.end(), id);
  if (it == openGestures_.end()) return;
  openGestures_.erase(it);
  push({UiEvent::Type::EndEdit, id, 0.0});
}

void UiBridge::closeAllGestures() {
  const std::vector<uint32_t> open = std::move(openGestures_);
  openGestures_.clear();
  for (const uint32_t id : open) push({UiEvent::Type::EndEdit, id, 0.0});
}

void UiBridge::detach() {
  requestFlush_ = {};
  requestPump_ = {};
}

void UiBridge::push(const UiEvent& event) {
  moveBacklog();
  if (!overflow_.empty() || !queue_.push(event)) overflow_.push_back(event);
  requestFlush();
  if (!overflow_.empty() && requestPump_ && !pumpPending_.exchange(true, std::memory_order_acq_rel)) {
    requestPump_();  // nothing else would retry the backlog if no further edit comes
  }
}

// The flag is a read-modify-write on both sides (acq_rel), so drain's clear and a push's
// request are totally ordered: either the push sees the clear and asks again, or drain's
// clear synchronizes with the push and its pops see the event. If a host ignores a
// request, the flag stays set until the next drain; the next process call drains anyway.
void UiBridge::requestFlush() {
  if (!flushPending_.exchange(true, std::memory_order_acq_rel) && requestFlush_) requestFlush_();
}

void UiBridge::moveBacklog() {
  bool moved = false;
  while (!overflow_.empty() && queue_.push(overflow_.front())) {
    overflow_.pop_front();
    moved = true;
  }
  if (moved) requestFlush();  // the moved events (maybe an END) wait for a drain
}

void UiBridge::pump() {
  pumpPending_.store(false, std::memory_order_release);
  moveBacklog();
  if (!overflow_.empty() && requestPump_ && !pumpPending_.exchange(true, std::memory_order_acq_rel)) {
    requestPump_();  // the queue is still full: try again after the audio thread drains
  }
}

void UiBridge::drain(const clap_output_events_t* out) {
  flushPending_.exchange(false, std::memory_order_acq_rel);  // events pushed from now on ask again
  UiEvent e;
  while (queue_.pop(e)) {
    const int index = paramIndex(e.id);
    if (index < 0) continue;
    switch (e.type) {
      case UiEvent::Type::BeginEdit:
      case UiEvent::Type::EndEdit:
        if (out != nullptr && !pushGesture(out, e.id, e.type == UiEvent::Type::BeginEdit)) {
          dropped_.fetch_add(1, std::memory_order_relaxed);
        }
        break;
      case UiEvent::Type::Edit: {
        if (std::isnan(e.value)) break;
        const float value = clampParam(size_t(index), e.value);
        synth_.setParam(e.id, double(value));
        if (out != nullptr && !pushValue(out, e.id, double(value))) {
          dropped_.fetch_add(1, std::memory_order_relaxed);
        }
        break;
      }
    }
  }
}

}  // namespace eg13
