#include "assets/GltfLoader.hpp"

#include "assets/GltfTextures.hpp"
#include "assets/MeshUpload.hpp"
#include "scene/SceneLight.hpp"
#include "scene/SceneMaterial.hpp"
#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/gtc/quaternion.hpp>
#include <spdlog/spdlog.h>

#include <fstream>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>
namespace genesis {

namespace {

std::vector<std::uint8_t> readFileBytes(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        throw std::runtime_error("Failed to open file: " + path.string());
    }
    const auto size = static_cast<size_t>(f.tellg());
    std::vector<std::uint8_t> bytes(size);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    return bytes;
}

struct VertexPNU {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
};

MeshVertexPNU toMeshVertex(const VertexPNU& v) {
    return MeshVertexPNU{v.px, v.py, v.pz, v.nx, v.ny, v.nz, v.u, v.v};
}

fastgltf::Asset loadAsset(const std::filesystem::path& path) {
    auto bytes = readFileBytes(path);
    fastgltf::Parser parser(fastgltf::Extensions::KHR_mesh_quantization |
                            fastgltf::Extensions::KHR_lights_punctual);
    auto data_expected = fastgltf::GltfDataBuffer::FromBytes(
        reinterpret_cast<const std::byte*>(bytes.data()), bytes.size());
    if (data_expected.error() != fastgltf::Error::None) {
        throw std::runtime_error("fastgltf: failed to create data buffer");
    }
    fastgltf::GltfDataBuffer data = std::move(data_expected.get());

    fastgltf::Options options = fastgltf::Options::LoadExternalBuffers |
                                fastgltf::Options::LoadExternalImages |
                                fastgltf::Options::DecomposeNodeMatrices;
    auto result = parser.loadGltf(data, path.parent_path(), options);
    if (result.error() != fastgltf::Error::None) {
        throw std::runtime_error("fastgltf parse failed");
    }
    return std::move(result.get());
}

AlphaMode toAlphaMode(fastgltf::AlphaMode mode) {
    switch (mode) {
    case fastgltf::AlphaMode::Mask:
        return AlphaMode::Mask;
    case fastgltf::AlphaMode::Blend:
        return AlphaMode::Blend;
    default:
        return AlphaMode::Opaque;
    }
}

std::string textureLabelFromIndex(const fastgltf::Asset& asset, std::size_t texture_index) {
    if (texture_index >= asset.textures.size()) {
        return "texture:" + std::to_string(texture_index);
    }
    const fastgltf::Texture& texture = asset.textures[texture_index];
    if (!texture.imageIndex.has_value() || *texture.imageIndex >= asset.images.size()) {
        return "texture:" + std::to_string(texture_index);
    }
    const fastgltf::Image& image = asset.images[*texture.imageIndex];
    if (!image.name.empty()) {
        return std::string(image.name);
    }
    if (const auto* uri = std::get_if<fastgltf::sources::URI>(&image.data)) {
        return std::string(uri->uri.string());
    }
    return "image:" + std::to_string(*texture.imageIndex);
}

std::string textureLabel(const fastgltf::Asset& asset, const fastgltf::Optional<fastgltf::TextureInfo>& info) {
    if (!info.has_value()) return {};
    return textureLabelFromIndex(asset, info->textureIndex);
}

std::string textureLabel(const fastgltf::Asset& asset, const fastgltf::Optional<fastgltf::NormalTextureInfo>& info) {
    if (!info.has_value()) return {};
    return textureLabelFromIndex(asset, info->textureIndex);
}

SceneMaterial importMaterial(const fastgltf::Asset& asset, const fastgltf::Material& src) {
    SceneMaterial out{};
    out.base_color = glm::vec4(static_cast<float>(src.pbrData.baseColorFactor.x()),
                               static_cast<float>(src.pbrData.baseColorFactor.y()),
                               static_cast<float>(src.pbrData.baseColorFactor.z()),
                               static_cast<float>(src.pbrData.baseColorFactor.w()));
    out.metallic = static_cast<float>(src.pbrData.metallicFactor);
    out.roughness = static_cast<float>(src.pbrData.roughnessFactor);
    out.emissive = glm::vec3(static_cast<float>(src.emissiveFactor.x()),
                             static_cast<float>(src.emissiveFactor.y()),
                             static_cast<float>(src.emissiveFactor.z()));
    out.emissive_strength = 1.0f;
    out.alpha_mode = toAlphaMode(src.alphaMode);
    out.alpha_cutoff = static_cast<float>(src.alphaCutoff);
    out.base_color_texture = textureLabel(asset, src.pbrData.baseColorTexture);
    out.metallic_roughness_texture = textureLabel(asset, src.pbrData.metallicRoughnessTexture);
    out.normal_texture = textureLabel(asset, src.normalTexture);
    out.emissive_texture = textureLabel(asset, src.emissiveTexture);
    return out;
}

std::optional<std::size_t> firstMaterialIndexForMesh(const fastgltf::Asset& asset, std::size_t mesh_index) {
    if (mesh_index >= asset.meshes.size()) return std::nullopt;
    const fastgltf::Mesh& mesh = asset.meshes[mesh_index];
    if (mesh.primitives.empty()) return std::nullopt;
    const fastgltf::Primitive& prim = mesh.primitives.front();
    if (!prim.materialIndex.has_value()) return std::nullopt;
    return *prim.materialIndex;
}

} // namespace

void GltfLoader::loadTexturesForFile(const std::filesystem::path& path, const VulkanContext& context,
                                     std::vector<SceneMaterial>& materials,
                                     std::deque<GpuTexture>& textures,
                                     std::vector<std::string>& texture_names) {
    const fastgltf::Asset asset = loadAsset(path);
    bindGltfMaterialTextures(asset, path.parent_path(), context, materials, textures, texture_names);
}

Mesh GltfLoader::loadFirstMesh(const VulkanContext& context, const std::filesystem::path& path) {
    fastgltf::Asset asset = loadAsset(path);

    if (asset.meshes.empty()) {
        throw std::runtime_error("glTF has no meshes: " + path.string());
    }
    const fastgltf::Mesh& mesh0 = asset.meshes[0];
    if (mesh0.primitives.empty()) {
        throw std::runtime_error("glTF mesh has no primitives");
    }
    const fastgltf::Primitive& prim = mesh0.primitives[0];

    const auto pos_it = prim.findAttribute("POSITION");
    const auto nrm_it = prim.findAttribute("NORMAL");
    if (pos_it == prim.attributes.cend() || nrm_it == prim.attributes.cend()) {
        throw std::runtime_error("Primitive missing POSITION or NORMAL");
    }
    if (!prim.indicesAccessor.has_value()) {
        throw std::runtime_error("Primitive missing indices");
    }

    const auto pos_acc = asset.accessors[pos_it->accessorIndex];
    const auto nrm_acc = asset.accessors[nrm_it->accessorIndex];
    const auto idx_acc = asset.accessors[*prim.indicesAccessor];

    const size_t vtx_count = static_cast<size_t>(pos_acc.count);
    if (nrm_acc.count != pos_acc.count) {
        throw std::runtime_error("NORMAL count does not match POSITION count");
    }

    std::vector<VertexPNU> vertices(vtx_count);
    std::vector<std::uint32_t> indices(static_cast<size_t>(idx_acc.count));

    {
        fastgltf::iterateAccessorWithIndex<glm::vec3>(asset, pos_acc, [&](glm::vec3 v, size_t i) {
            vertices[i].px = v.x;
            vertices[i].py = v.y;
            vertices[i].pz = v.z;
        });
        fastgltf::iterateAccessorWithIndex<glm::vec3>(asset, nrm_acc, [&](glm::vec3 v, size_t i) {
            vertices[i].nx = v.x;
            vertices[i].ny = v.y;
            vertices[i].nz = v.z;
        });
    }

    if (const auto uv_it = prim.findAttribute("TEXCOORD_0"); uv_it != prim.attributes.cend()) {
        const auto& uv_acc = asset.accessors[uv_it->accessorIndex];
        fastgltf::iterateAccessorWithIndex<glm::vec2>(asset, uv_acc, [&](glm::vec2 uv, size_t i) {
            vertices[i].u = uv.x;
            vertices[i].v = uv.y;
        });
    }

    // Indices may be u16/u32. Convert to u32.
    switch (idx_acc.componentType) {
    case fastgltf::ComponentType::UnsignedShort:
        fastgltf::iterateAccessorWithIndex<std::uint16_t>(asset, idx_acc, [&](std::uint16_t v, size_t i) {
            indices[i] = static_cast<std::uint32_t>(v);
        });
        break;
    case fastgltf::ComponentType::UnsignedInt:
        fastgltf::iterateAccessorWithIndex<std::uint32_t>(asset, idx_acc, [&](std::uint32_t v, size_t i) { indices[i] = v; });
        break;
    default:
        throw std::runtime_error("Unsupported index component type");
    }

    Mesh out{};
    std::vector<MeshVertexPNU> mesh_vertices;
    mesh_vertices.reserve(vertices.size());
    for (const auto& v : vertices) {
        mesh_vertices.push_back(toMeshVertex(v));
    }
    out = uploadMesh(context, mesh_vertices, indices);

    spdlog::info("Loaded glTF mesh: vertices={}, indices={}", vertices.size(), indices.size());
    return out;
}

Scene GltfLoader::loadScene(const std::filesystem::path& path,
                            std::vector<SceneMaterial>* out_materials) {
    fastgltf::Asset asset = loadAsset(path);

    if (out_materials) {
        out_materials->clear();
        out_materials->reserve(asset.materials.size());
        for (const fastgltf::Material& src : asset.materials) {
            out_materials->push_back(importMaterial(asset, src));
        }
        if (out_materials->empty()) {
            out_materials->push_back(SceneMaterial{});
        }
    }

    Scene scene{};
    scene.nodes.reserve(asset.nodes.size());

    const auto toNodeId = [](size_t i) -> NodeId { return static_cast<NodeId>(i + 1); };

    // Create nodes (without children wired yet).
    for (size_t i = 0; i < asset.nodes.size(); ++i) {
        const fastgltf::Node& src = asset.nodes[i];
        Node n{};
        n.id = toNodeId(i);
        n.name = src.name.empty() ? ("Node " + std::to_string(i)) : std::string(src.name);
        n.has_mesh = src.meshIndex.has_value();
        if (src.meshIndex.has_value() && out_materials) {
            if (const auto mat_index = firstMaterialIndexForMesh(asset, *src.meshIndex)) {
                if (*mat_index < out_materials->size()) {
                    n.has_material = true;
                    n.material_resource = static_cast<std::uint32_t>(*mat_index);
                }
            } else {
                n.has_material = true;
                n.material_resource = 0;
            }
        }
        n.has_camera = src.cameraIndex.has_value();        n.has_light = src.lightIndex.has_value();
        if (src.lightIndex.has_value()) {
            if (*src.lightIndex >= asset.lights.size()) {
                spdlog::warn(
                    "Node '{}' references light {} but asset defines {} KHR_lights_punctual light(s)",
                    n.name, *src.lightIndex, asset.lights.size());
            } else {
                const fastgltf::Light& src_light = asset.lights[*src.lightIndex];
                SceneLight light{};
                light.color = glm::vec3(static_cast<float>(src_light.color.x()),
                                        static_cast<float>(src_light.color.y()),
                                        static_cast<float>(src_light.color.z()));
                light.intensity = static_cast<float>(src_light.intensity);
                if (src_light.range.has_value()) {
                    light.range = static_cast<float>(*src_light.range);
                }
                switch (src_light.type) {
                case fastgltf::LightType::Directional:
                    light.type = SceneLightType::Directional;
                    break;
                case fastgltf::LightType::Point:
                    light.type = SceneLightType::Point;
                    break;
                case fastgltf::LightType::Spot:
                    light.type = SceneLightType::Spot;
                    if (src_light.innerConeAngle.has_value()) {
                        light.inner_cone_radians = static_cast<float>(*src_light.innerConeAngle);
                    }
                    if (src_light.outerConeAngle.has_value()) {
                        light.outer_cone_radians = static_cast<float>(*src_light.outerConeAngle);
                    }
                    break;
                default:
                    break;
                }
                n.light = light;
            }
        }

        if (src.cameraIndex.has_value() && *src.cameraIndex < asset.cameras.size()) {
            const fastgltf::Camera& src_cam = asset.cameras[*src.cameraIndex];
            if (const auto* persp = std::get_if<fastgltf::Camera::Perspective>(&src_cam.camera)) {
                SceneCamera cam{};
                cam.fov_y_radians = static_cast<float>(persp->yfov);
                cam.near_plane = static_cast<float>(persp->znear);
                cam.far_plane = persp->zfar.has_value() ? static_cast<float>(*persp->zfar) : 200.0f;
                n.camera = cam;
            }
        }

        // With Options::DecomposeNodeMatrices enabled, this should usually be TRS.
        if (std::holds_alternative<fastgltf::TRS>(src.transform)) {
            const auto& t = std::get<fastgltf::TRS>(src.transform);
            n.local.position = glm::vec3(t.translation.x(), t.translation.y(), t.translation.z());
            n.local.rotation = glm::quat(t.rotation.w(), t.rotation.x(), t.rotation.y(), t.rotation.z());
            n.local.scale = glm::vec3(t.scale.x(), t.scale.y(), t.scale.z());
        } else {
            // Fallback: keep identity; we’ll add matrix decomposition if we hit assets that need it.
        }

        scene.nodes.push_back(std::move(n));
    }

    // Wire hierarchy.
    for (size_t i = 0; i < asset.nodes.size(); ++i) {
        const fastgltf::Node& src = asset.nodes[i];
        Node& dst = scene.nodes[i];
        for (const auto child_index : src.children) {
            const NodeId child_id = toNodeId(static_cast<size_t>(child_index));
            dst.children.push_back(child_id);
            if (Node* child = scene.find(child_id)) {
                child->parent = dst.id;
            }
        }
    }

    // Roots: prefer the default scene if present, otherwise any node without a parent.
    if (asset.defaultScene.has_value() && *asset.defaultScene < asset.scenes.size()) {
        const auto& gltf_scene = asset.scenes[*asset.defaultScene];
        for (const auto root_index : gltf_scene.nodeIndices) {
            scene.roots.push_back(toNodeId(static_cast<size_t>(root_index)));
        }
    } else {
        for (const auto& n : scene.nodes) {
            if (!n.parent.has_value()) scene.roots.push_back(n.id);
        }
    }

    scene.updateWorldTransforms();
    spdlog::info("Loaded glTF scene: nodes={}, roots={}, materials={}", scene.nodes.size(),
                 scene.roots.size(), out_materials ? out_materials->size() : 0);
    return scene;
}
} // namespace genesis

