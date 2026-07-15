#pragma once

#include "rxext_client.h"
#include "scene/Scene.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace rxext::genesis_ext {

struct SceneSelectorLists {
    std::vector<std::string> show_paths;
    std::vector<std::string> actor_paths;
    std::vector<std::string> light_paths;
};

// Build dropdown lists from the loaded scene graph.
SceneSelectorLists buildSceneSelectorLists(const genesis::Scene& scene);

std::int32_t findSelectorIndex(const std::vector<std::string>& options, const std::string& value,
                               std::int32_t fallback = 0);

void setShowEnumOptions(ParameterInt* parameter, const std::vector<std::string>& show_paths,
                        std::int32_t selected_index);

void setNodeEnumOptions(ParameterInt* parameter, const std::vector<std::string>& node_paths,
                        std::int32_t selected_index);

} // namespace rxext::genesis_ext
