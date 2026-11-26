#pragma once

#include "protocol/OscProtocol.hpp"

namespace genesis {

// View → Room Tracking — author people / wall-ray / TUIO with the scene (no silent magic).
// Returns true when settings changed (caller can mark the document dirty).
bool drawRoomTrackingPanel(OscProtocol& protocol, bool* open);

} // namespace genesis
