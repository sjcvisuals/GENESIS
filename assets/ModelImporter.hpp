#pragma once

#include "animation/AnimationClip.hpp"
#include "animation/AnimationGraph.hpp"
#include "scene/SceneRuntime.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace genesis {

struct ModelImportResult {
    bool ok = false;
    std::string error;
    NodeId root_node = 0;
    std::vector<std::uint32_t> mesh_ids;
    std::vector<std::uint32_t> material_ids;
    std::vector<std::int32_t> clip_indices;
    std::string source_path;
};

class ModelImporter {
public:
    // Additive import into an existing scene (does not clear the scene).
    static ModelImportResult importInto(SceneRuntime& runtime, const std::filesystem::path& path);
};

bool isSupportedModelExtension(const std::filesystem::path& path);

} // namespace genesis
