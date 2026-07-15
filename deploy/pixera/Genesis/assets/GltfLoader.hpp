#pragma once

#include "renderer/Mesh.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "scene/Scene.hpp"
#include "scene/SceneMaterial.hpp"

#include <filesystem>
#include <vector>

namespace genesis {

class GltfLoader {
public:
    static Mesh loadFirstMesh(const VulkanContext& context, const std::filesystem::path& path);

    static Scene loadScene(const std::filesystem::path& path,
                           std::vector<SceneMaterial>* out_materials = nullptr);
};

} // namespace genesis
