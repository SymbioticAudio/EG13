#include "ui_bridge.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "synth/param_table.h"

namespace eg13 {

namespace {

void pushGesture(const clap_output_events_t* out, uint32_t id, bool begin) {
  clap_event_param_gesture_t ev{};
  ev.header.size = sizeof(ev);
  ev.header.time = 0;
  ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
  ev.header.type = begin ? CLAP_EVENT_PARAM_GESTURE_BEGIN : CLAP_EVENT_PARAM_GESTURE_END;
  ev.header.flags = 0;
  ev.param_id = id;
  out->try_push(out, &ev.header);
}

void pushValue(const clap_output_events_t* out, uint32_t id, double value) {
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
  out->try_push(out, &ev.header);
}

}  // namespace

UiBridge::UiBridge(Synth& synth, std::function<void()> requestFlush)
    : synth_(synth), requestFlush_(std::move(requestFlush)) {}

UiBridge::~UiBridge() { closeAllGestures(); }

void UiBridge::beginEdit(uint32_t id) {
  if (std::find(openGestures_.begin(), openGestures_.end(), id) == openGestures_.end()) {
    openGestures_.push_back(id);
  }
  push({UiEvent::Type::BeginEdit, id, 0.0});
}

void UiBridge::edit(uint32_t id, double v) { push({UiEvent::Type::Edit, id, v}); }

void UiBridge::endEdit(uint32_t id) {
  openGestures_.erase(std::remove(openGestures_.begin(), openGestures_.end(), id),
                      openGestures_.end());
  push({UiEvent::Type::EndEdit, id, 0.0});
}

void UiBridge::closeAllGestures() {
  const std::vector<uint32_t> open = std::move(openGestures_);
  openGestures_.clear();
  for (const uint32_t id : open) push({UiEvent::Type::EndEdit, id, 0.0});
}

void UiBridge::push(const UiEvent& event) {
  pump();
  if (!overflow_.empty() || !queue_.push(event)) overflow_.push_back(event);
  if (requestFlush_) requestFlush_();
}

void UiBridge::pump() {
  while (!overflow_.empty() && queue_.push(overflow_.front())) overflow_.pop_front();
}

void UiBridge::drain(const clap_output_events_t* out) {
  UiEvent e;
  while (queue_.pop(e)) {
    const int index = paramIndex(e.id);
    if (index < 0) continue;
    switch (e.type) {
      case UiEvent::Type::BeginEdit:
      case UiEvent::Type::EndEdit:
        if (out != nullptr) pushGesture(out, e.id, e.type == UiEvent::Type::BeginEdit);
        break;
      case UiEvent::Type::Edit: {
        if (std::isnan(e.value)) break;
        const float value = clampParam(size_t(index), e.value);
        synth_.setParam(e.id, double(value));
        if (out != nullptr) pushValue(out, e.id, double(value));
        break;
      }
    }
  }
}

}  // namespace eg13
