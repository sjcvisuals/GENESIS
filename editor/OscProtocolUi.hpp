#pragma once

#include "protocol/OscProtocol.hpp"

namespace genesis {

struct OscLinkUiContext {
    OscProtocol* protocol = nullptr;
};

void drawOscProtocolPanel(OscProtocol& protocol, bool* open);
bool drawOscLinkButton(OscLinkUiContext& ctx, OscParamTarget target, const std::string& node_path,
                       std::uint32_t material_id = 0, const char* id_suffix = "");

} // namespace genesis
