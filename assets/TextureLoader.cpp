#include "assets/TextureLoader.hpp"

#include <stb/stb_image.h>

#include <stdexcept>
#include <vector>

namespace genesis {

LoadedImageTexture loadImageTexture(const VulkanContext& context, const std::filesystem::path& path,
                                    const TextureColorSpace color_space, const bool treat_as_color_map) {
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
    if (!pixels || width <= 0 || height <= 0) {
        stbi_image_free(pixels);
        throw std::runtime_error("Failed to load image: " + path.string());
    }

    bool use_srgb = treat_as_color_map;
    if (color_space == TextureColorSpace::sRGB) {
        use_srgb = true;
    } else if (color_space == TextureColorSpace::Linear) {
        use_srgb = false;
    }

    LoadedImageTexture out{
        GpuTexture(context, pixels, static_cast<std::uint32_t>(width),
                   static_cast<std::uint32_t>(height),
                   use_srgb ? TextureColorEncoding::sRGB : TextureColorEncoding::Linear),
        static_cast<std::uint32_t>(width),
        static_cast<std::uint32_t>(height),
        path.filename().string(),
    };
    stbi_image_free(pixels);
    return out;
}

} // namespace genesis
