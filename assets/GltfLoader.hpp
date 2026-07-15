#pragma once

#include "renderer/Mesh.hpp"
#include "renderer/vulkan/Texture.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "scene/Scene.hpp"
#include "scene/SceneMaterial.hpp"

#include <filesystem>
#include <vector>

namespace genesis {

class GltfLoader {
public:
    static Mesh loadFirstMesh(const VulkanContext& context, const std::filesystem::path& path);

    static void loadTexturesForFile(const std::filesystem::path& path, const VulkanContext& context,
                                    std::vector<SceneMaterial>& materials,
                                    std::vector<GpuTexture>& textures,
                                    std::vector<std::string>& texture_names);

    static Scene loadScene(const std::filesystem::path& path,
                           std::vector<SceneMaterial>* out_materials = nullptr);
};

} // namespace genesis
