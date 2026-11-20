#pragma once

#include "runtime/PerfDebug.hpp"

namespace genesis {

// ImGui helpers for the editor / HostBridge performance overlay.
void drawPerfDebugOverlayWindow(const PerfDebugSnapshot& snap, bool* open = nullptr);
void drawPerfDebugControls(bool& show_overlay, bool& show_on_output);

} // namespace genesis
