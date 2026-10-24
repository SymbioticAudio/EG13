#pragma once

// The editor window as the CLAP gui extension sees it. Only built when the SwiftUI editor is
// (EG13_HAS_UI); the C entry points it calls are core-api/include/eg13/ui_entry.h.

#include <cstdint>
#include <functional>

#include "eg13/core_api.h"
#include "ui_bridge.h"

namespace eg13 {

// Logical points. 1200 x 760 at 100 %; the window scales from 75 % to 200 % at a fixed aspect.
constexpr uint32_t kGuiBaseWidth = 1200;
constexpr uint32_t kGuiBaseHeight = 760;
constexpr uint32_t kGuiMinWidth = 900;    // 75 %
constexpr uint32_t kGuiMinHeight = 570;
constexpr uint32_t kGuiMaxWidth = 2400;   // 200 %
constexpr uint32_t kGuiMaxHeight = 1520;

// The size nearest the request that keeps 1200:760 (width and height are multiples of 30 and
// 19) and lies between the minimum and maximum. The larger of the two requested scales wins,
// so dragging either edge resizes the window.
void adjustGuiSize(uint32_t& width, uint32_t& height);

// One plugin instance's editor. Main thread only. The bridge and the API table must outlive it.
class Gui {
 public:
  // kind: 0 = EG13 (instrument), 1 = EG13 FX. requestFlush asks the host for a params flush
  // so the gesture ends that destroy() queues reach it; it may be empty.
  Gui(const EG13CoreAPI& api, int32_t kind, UiBridge& bridge, std::function<void()> requestFlush);
  ~Gui();
  Gui(const Gui&) = delete;
  Gui& operator=(const Gui&) = delete;

  bool create();   // false when the editor could not be built; true if it already exists
  // Ends open gestures (closeAllGestures), destroys the editor, then asks the host to flush.
  // The bridge stays: its queued END events drain through process or flush.
  // flushAfter false skips the host flush request (the plugin itself is going away).
  void destroy(bool flushAfter = true);
  bool exists() const { return handle_ != nullptr; }

  bool setParent(void* nsView);
  void setSize(uint32_t width, uint32_t height);
  void setVisible(bool visible);
  uint32_t width() const { return width_; }
  uint32_t height() const { return height_; }

 private:
  const EG13CoreAPI& api_;
  int32_t kind_;
  UiBridge& bridge_;
  std::function<void()> requestFlush_;
  void* handle_ = nullptr;
  uint32_t width_ = kGuiBaseWidth;
  uint32_t height_ = kGuiBaseHeight;
};

}  // namespace eg13
