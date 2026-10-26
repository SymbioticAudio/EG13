#pragma once

#include <cstdint>
#include <functional>

#include "eg13/core_api.h"
#include "synth/synth.h"
#include "ui_bridge.h"

namespace eg13 {

// What the C function table needs from the plugin: the synth to read, the bridge to edit through,
// and where a resize request goes (the plugin forwards it to the host's gui extension).
// Main thread only. The context must outlive the table and every editor that holds it.
struct CoreApiContext {
  Synth& synth;
  UiBridge& bridge;
  std::function<void(uint32_t width, uint32_t height)> requestResize;  // may be empty
};

// The function table over ctx. Its `context` points at ctx.
EG13CoreAPI makeCoreApi(CoreApiContext& ctx);

}  // namespace eg13
