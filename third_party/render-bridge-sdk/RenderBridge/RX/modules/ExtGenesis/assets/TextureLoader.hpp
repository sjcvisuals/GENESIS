#pragma once

#include "color/ColorManagement.hpp"
#include "renderer/vulkan/Texture.hpp"

#include <filesystem>
#include <string>

namespace genesis {

class VulkanContext;

struct LoadedImageTexture {
    GpuTexture texture;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::string label;
};

// Load PNG/JPEG/TGA/BMP via stb_image into a GPU texture.
// color_space Auto: sRGB for albedo/emissive-style maps, linear for data maps.
LoadedImageTexture loadImageTexture(const VulkanContext& context, const std::filesystem::path& path,
                                    TextureColorSpace color_space = TextureColorSpace::Auto,
                                    bool treat_as_color_map = true);

} // namespace genesis
