#include "gui.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "eg13/ui_entry.h"

namespace eg13 {

void adjustGuiSize(uint32_t& width, uint32_t& height) {
  // 1200:760 is 30:19, so the window is 30k x 19k points for a whole k (k = 40 at 100 %).
  const double k = std::max(double(width) / 30.0, double(height) / 19.0);
  const double kMin = kGuiMinWidth / 30.0, kMax = kGuiMaxWidth / 30.0;
  const uint32_t units = uint32_t(std::lround(std::min(std::max(k, kMin), kMax)));
  width = 30 * units;
  height = 19 * units;
}

Gui::Gui(const EG13CoreAPI& api, int32_t kind, UiBridge& bridge, std::function<void()> requestFlush)
    : api_(api), kind_(kind), bridge_(bridge), requestFlush_(std::move(requestFlush)) {}

Gui::~Gui() { destroy(); }

bool Gui::create() {
  if (handle_ != nullptr) return true;
  handle_ = eg13_ui_create(&api_, kind_, 1.0);
  width_ = kGuiBaseWidth;
  height_ = kGuiBaseHeight;
  return handle_ != nullptr;
}

void Gui::destroy() {
  if (handle_ == nullptr) return;
  bridge_.closeAllGestures();
  eg13_ui_destroy(handle_);
  handle_ = nullptr;
  if (requestFlush_) requestFlush_();
}

bool Gui::setParent(void* nsView) {
  return handle_ != nullptr && eg13_ui_attach(handle_, nsView);
}

void Gui::setSize(uint32_t width, uint32_t height) {
  width_ = width;
  height_ = height;
  if (handle_ != nullptr) eg13_ui_set_size(handle_, width, height);
}

void Gui::setVisible(bool visible) {
  if (handle_ != nullptr) eg13_ui_set_visible(handle_, visible);
}

}  // namespace eg13
