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
  // requestPump_ is not reassigned: drain() reads it on the audio thread, so the flag is the
  // only thing that changes. Both sides check detached_ before calling the hook.
  detached_.store(true, std::memory_order_release);
  requestFlush_ = {};
}

void UiBridge::push(const UiEvent& event) {
  moveBacklog();
  if (!overflow_.empty() || !queue_.push(event)) overflow_.push_back(event);
  requestFlush();
  if (overflow_.empty()) return;
  markBacklog();
  // The first request for a backlog. Nothing may be draining (host idle, no params flush), and
  // without a pump the backlog would wait for the next edit. While the editor is open the 30 Hz
  // snapshot calls pump() as well, so a backlog is retried even if the host ignores this
  // request. Coalesced: one outstanding request until pump() runs. pump() never asks again;
  // every later request comes from drain().
  if (requestPump_ && !detached_.load(std::memory_order_acquire) &&
      !pumpPending_.exchange(true, std::memory_order_acq_rel)) {
    requestPump_();
  }
}

// No lost wake-up. The main thread fails queue_.push (queue full), then sets backlogPending_
// (acq_rel read-modify-write), then retries moveBacklog. drain() pops, then clears the flag
// (acq_rel exchange) and asks for a pump if it was set. The flag operations are totally
// ordered. If drain's exchange comes after the set, it sees true and requests a pump. If it
// comes before the set, every pop that freed space happened before the exchange, hence before
// the set, hence before the retry, which therefore sees the space and moves the backlog (and
// requestFlush asks for a drain). A pop that comes after the retry belongs to a later drain,
// whose exchange follows the set. So a backlog that exists after the retry always has a
// drain still to come that requests a pump. A stale true flag costs one extra, harmless pump.
void UiBridge::markBacklog() {
  backlogPending_.exchange(true, std::memory_order_acq_rel);
  moveBacklog();
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
  // Never asks for another pump itself: with a host that does not drain, that loops through
  // on_main_thread forever. If the queue is still full, the flag makes the next drain ask.
  if (!overflow_.empty()) markBacklog();
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
  // Space was freed: ask for exactly one pump if the main thread has a backlog. CLAP allows
  // request_callback from any thread. Order argument: see markBacklog.
  if (backlogPending_.exchange(false, std::memory_order_acq_rel) && requestPump_ &&
      !detached_.load(std::memory_order_acquire)) {
    requestPump_();
  }
}

}  // namespace eg13
