#pragma once

#include "scene/SceneMaterial.hpp"

#include <cstdint>
#include <deque>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace fastgltf {
struct Asset;
}

namespace genesis {

class VulkanContext;
class GpuTexture;

struct DecodedImage {
    std::vector<std::uint8_t> rgba;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

bool decodeGltfImage(const fastgltf::Asset& asset, std::size_t image_index,
                     const std::filesystem::path& base_dir, DecodedImage& out);

void bindGltfMaterialTextures(const fastgltf::Asset& asset,
                               const std::filesystem::path& base_dir,
                               const VulkanContext& context,
                               std::vector<SceneMaterial>& materials,
                               std::deque<GpuTexture>& textures,
                               std::vector<std::string>& texture_names);

} // namespace genesis
