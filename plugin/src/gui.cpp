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

Gui::Gui(const EG13CoreAPI& api, int32_t kind, UiBridge& bridge, std::function<void()> requestFlush,
         std::function<bool(uint32_t, uint32_t)> hostResize)
    : api_(api), kind_(kind), bridge_(bridge), requestFlush_(std::move(requestFlush)),
      hostResize_(std::move(hostResize)) {}

Gui::~Gui() { destroy(false); }

bool Gui::create() {
  if (handle_ != nullptr) return true;
  // The window opens at the settings' default size. get_size reports it from here on; the editor
  // reads the same setting for its zoom (0 = "use the default"), and set_size below keeps the two
  // equal even if the file changed in between.
  const double zoom = eg13_ui_default_zoom();
  uint32_t width = uint32_t(std::lround(double(kGuiBaseWidth) * zoom));
  uint32_t height = uint32_t(std::lround(double(kGuiBaseHeight) * zoom));
  adjustGuiSize(width, height);
  handle_ = eg13_ui_create(&api_, kind_, 0.0);
  width_ = width;
  height_ = height;
  if (handle_ != nullptr) eg13_ui_set_size(handle_, width_, height_);
  return handle_ != nullptr;
}

void Gui::destroy(bool flushAfter) {
  if (handle_ == nullptr) return;
  bridge_.closeAllGestures();
  eg13_ui_destroy(handle_);
  handle_ = nullptr;
  if (flushAfter && requestFlush_) requestFlush_();
}

bool Gui::setParent(void* nsView) {
  return handle_ != nullptr && eg13_ui_attach(handle_, nsView);
}

void Gui::setSize(uint32_t width, uint32_t height) {
  // The host should have called adjust_size, but set_size may come with any size.
  adjustGuiSize(width, height);
  width_ = width;
  height_ = height;
  if (handle_ != nullptr) eg13_ui_set_size(handle_, width, height);
}

bool Gui::requestResize(uint32_t width, uint32_t height) {
  adjustGuiSize(width, height);
  if (!hostResize_ || !hostResize_(width, height)) return false;
  setSize(width, height);  // idempotent if the host also called set_size
  return true;
}

void Gui::setVisible(bool visible) {
  if (handle_ != nullptr) eg13_ui_set_visible(handle_, visible);
}

}  // namespace eg13
