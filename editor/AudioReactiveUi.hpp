#pragma once

#include "audio/AudioReactive.hpp"

namespace genesis {

struct AudioLinkUiContext {
    AudioReactive* reactive = nullptr;
};

void drawAudioReactivePanel(AudioReactive& reactive, bool* open,
                            const std::string& selected_fluid_node_path = {});

bool drawAudioLinkButton(AudioLinkUiContext& ctx, OscParamTarget target, const std::string& node_path,
                         std::uint32_t material_id = 0, const char* id_suffix = "");

} // namespace genesis
