#include "assets/GltfTextures.hpp"

#include "renderer/vulkan/Texture.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>

#include <spdlog/spdlog.h>

#include <deque>
#include <fstream>
#include <optional>
#include <stdexcept>

namespace genesis {

namespace {

std::uint32_t findOrAddTexture(std::deque<GpuTexture>& textures, std::vector<std::string>& names,
                               const std::string& label, GpuTexture&& texture) {
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (names[i] == label) {
            return static_cast<std::uint32_t>(i);
        }
    }
    names.push_back(label);
    textures.push_back(std::move(texture));
    return static_cast<std::uint32_t>(textures.size() - 1);
}

bool decodeImageBytes(const std::uint8_t* data, int size, DecodedImage& out) {
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(data, size, &width, &height, &channels, 4);
    if (!pixels || width <= 0 || height <= 0) {
        stbi_image_free(pixels);
        return false;
    }
    out.width = static_cast<std::uint32_t>(width);
    out.height = static_cast<std::uint32_t>(height);
    out.rgba.assign(pixels, pixels + static_cast<std::size_t>(width * height * 4));
    stbi_image_free(pixels);
    return true;
}

std::optional<std::uint32_t> loadGltfTextureByIndex(const fastgltf::Asset& asset,
                                                    const std::filesystem::path& base_dir,
                                                    const VulkanContext& context,
                                                    std::size_t texture_index,
                                                    std::deque<GpuTexture>& textures,
                                                    std::vector<std::string>& texture_names,
                                                    TextureColorEncoding encoding) {
    if (texture_index >= asset.textures.size()) {
        return std::nullopt;
    }
    const fastgltf::Texture& texture = asset.textures[texture_index];
    if (!texture.imageIndex.has_value()) {
        return std::nullopt;
    }

    DecodedImage decoded{};
    if (!decodeGltfImage(asset, *texture.imageIndex, base_dir, decoded)) {
        return std::nullopt;
    }

    std::string label;
    if (*texture.imageIndex < asset.images.size() && !asset.images[*texture.imageIndex].name.empty()) {
        label = std::string(asset.images[*texture.imageIndex].name);
    } else {
        label = "image:" + std::to_string(*texture.imageIndex);
    }

    return findOrAddTexture(textures, texture_names, label,
                            GpuTexture(context, decoded.rgba.data(), decoded.width, decoded.height, encoding));
}

template <typename TextureInfoT>
void bindOptionalMap(const fastgltf::Asset& asset, const std::filesystem::path& base_dir,
                     const VulkanContext& context, const TextureInfoT& info,
                     std::optional<std::uint32_t>& out_resource, std::string& out_label,
                     std::deque<GpuTexture>& textures, std::vector<std::string>& texture_names,
                     const char* map_name, std::size_t material_index, TextureColorEncoding encoding) {
    if (!info.has_value()) {
        return;
    }
    const auto texture_id = loadGltfTextureByIndex(asset, base_dir, context, info->textureIndex, textures,
                                                   texture_names, encoding);
    if (!texture_id.has_value()) {
        spdlog::warn("GENESIS failed to decode glTF {} image for material {}", map_name, material_index);
        return;
    }
    out_resource = *texture_id;
    if (out_label.empty()) {
        out_label = texture_names[*texture_id];
    }
}

} // namespace

bool decodeGltfImage(const fastgltf::Asset& asset, const std::size_t image_index,
                     const std::filesystem::path& base_dir, DecodedImage& out) {
    if (image_index >= asset.images.size()) {
        return false;
    }

    const fastgltf::Image& image = asset.images[image_index];
    return std::visit(
        fastgltf::visitor{
            [](const auto&) { return false; },
            [&](const fastgltf::sources::URI& uri) {
                if (!uri.uri.isLocalPath()) {
                    return false;
                }
                const std::filesystem::path path = base_dir / uri.uri.path();
                std::ifstream file(path, std::ios::binary | std::ios::ate);
                if (!file) {
                    return false;
                }
                const auto size = static_cast<int>(file.tellg());
                std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
                file.seekg(0);
                file.read(reinterpret_cast<char*>(bytes.data()), size);
                return decodeImageBytes(bytes.data(), size, out);
            },
            [&](const fastgltf::sources::Array& array) {
                return decodeImageBytes(reinterpret_cast<const std::uint8_t*>(array.bytes.data()),
                                        static_cast<int>(array.bytes.size()), out);
            },
            [&](const fastgltf::sources::BufferView& view) {
                if (view.bufferViewIndex >= asset.bufferViews.size()) {
                    return false;
                }
                const fastgltf::DefaultBufferDataAdapter adapter{};
                const auto bytes = adapter(asset, view.bufferViewIndex);
                if (bytes.empty()) {
                    return false;
                }
                return decodeImageBytes(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                        static_cast<int>(bytes.size()), out);
            },
        },
        image.data);
}

void bindGltfMaterialTextures(const fastgltf::Asset& asset,
                               const std::filesystem::path& base_dir,
                               const VulkanContext& context,
                               std::vector<SceneMaterial>& materials,
                               std::deque<GpuTexture>& textures,
                               std::vector<std::string>& texture_names) {
    for (std::size_t material_index = 0; material_index < asset.materials.size(); ++material_index) {
        if (material_index >= materials.size()) {
            break;
        }
        const fastgltf::Material& src = asset.materials[material_index];
        SceneMaterial& dst = materials[material_index];

        bindOptionalMap(asset, base_dir, context, src.pbrData.baseColorTexture,
                        dst.base_color_texture_resource, dst.base_color_texture, textures, texture_names,
                        "base color", material_index, TextureColorEncoding::sRGB);
        bindOptionalMap(asset, base_dir, context, src.pbrData.metallicRoughnessTexture,
                        dst.metallic_roughness_texture_resource, dst.metallic_roughness_texture, textures,
                        texture_names, "metallic-roughness", material_index, TextureColorEncoding::Linear);
        bindOptionalMap(asset, base_dir, context, src.normalTexture, dst.normal_texture_resource,
                        dst.normal_texture, textures, texture_names, "normal", material_index,
                        TextureColorEncoding::Linear);
        bindOptionalMap(asset, base_dir, context, src.emissiveTexture, dst.emissive_texture_resource,
                        dst.emissive_texture, textures, texture_names, "emissive", material_index,
                        TextureColorEncoding::sRGB);

        if (src.normalTexture.has_value()) {
            dst.normal_scale = static_cast<float>(src.normalTexture->scale);
        }
    }
}

} // namespace genesis
