#pragma once

#include "renderer/Mesh.hpp"
#include "renderer/MeshDrawPushConstants.hpp"
#include "renderer/vulkan/Texture.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "scene/Scene.hpp"
#include "scene/SceneDocument.hpp"
#include "scene/SceneMaterial.hpp"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace genesis {

struct SceneDrawCommand {
    NodeId node_id = 0;
    const Mesh* mesh = nullptr;
    const GpuTexture* base_color_texture = nullptr;
    MeshDrawPushConstants constants{};
};

struct SceneRenderStats {
    std::uint32_t draw_calls = 0;
    std::uint32_t triangles = 0;
    std::uint32_t visible_mesh_nodes = 0;
    std::uint32_t scene_nodes = 0;
    std::uint32_t active_lights = 0;
};

class SceneRuntime {
public:
    explicit SceneRuntime(const VulkanContext& context);

    void clear();

    void loadGltf(const std::filesystem::path& path);
    // Absolute paths for host integrations (PIXERA storage, extension assets).
    void loadGltfFile(const std::filesystem::path& absolute_path);
    void loadDocument(const std::filesystem::path& path);
    void loadDocumentFile(const std::filesystem::path& absolute_path);
    void loadDocumentData(const SceneDocument& document, const std::filesystem::path& asset_root);
    void loadGnsFile(const std::filesystem::path& absolute_path);
    void saveDocument(const std::filesystem::path& path);
    void loadDemo();

    Scene& scene() { return scene_; }
    const Scene& scene() const { return scene_; }

    Node* findNode(NodeId id) { return scene_.find(id); }
    const Node* findNode(NodeId id) const { return scene_.find(id); }
    Node* findNodeByPath(const std::string& path);
    const Node* findNodeByPath(const std::string& path) const;

    std::uint32_t addMesh(std::string name, Mesh mesh);
    std::uint32_t addMaterial(std::string name, SceneMaterial material);
    std::uint32_t addTexture(std::string name, GpuTexture texture);

    Mesh* mesh(std::uint32_t id);
    const Mesh* mesh(std::uint32_t id) const;
    SceneMaterial* material(std::uint32_t id);
    const SceneMaterial* material(std::uint32_t id) const;
    GpuTexture* texture(std::uint32_t id);
    const GpuTexture* texture(std::uint32_t id) const;

    const SceneMaterial& resolveMaterial(const Node& node) const;
    std::uint32_t resolveMeshRef(const std::string& ref);
    std::uint32_t resolveMaterialRef(const std::string& ref);

    void updateTransforms();

    void collectDrawCommands(std::vector<SceneDrawCommand>& out,
                             SceneRenderStats* out_stats = nullptr) const;

    std::optional<glm::mat4> outputCameraViewProjection(float aspect) const;
    std::optional<glm::vec3> outputCameraPosition() const;

    const std::string& sourcePath() const { return source_path_; }
    const std::string& documentPath() const { return document_path_; }
    const std::vector<std::string>& meshNames() const { return mesh_names_; }
    const std::vector<std::string>& materialNames() const { return material_names_; }
    const std::vector<Mesh>& meshes() const { return meshes_; }
    const std::vector<SceneMaterial>& materials() const { return materials_; }
    const std::vector<GpuTexture>& textures() const { return textures_; }
    const std::vector<std::string>& textureNames() const { return texture_names_; }

private:
    void loadGltfSource(const std::filesystem::path& path);
    void applyDocumentOverrides(const SceneDocument& document);
    void buildFromDocument(const SceneDocument& document);
    void applyNodeEntry(NodeId id, const SceneDocumentNode& entry);
    void createNodeFromEntry(const SceneDocumentNode& entry);
    void finishDocumentLoad(const SceneDocument& document);

    const VulkanContext& context_;
    Scene scene_{};
    std::vector<Mesh> meshes_;
    std::vector<std::string> mesh_names_;
    std::vector<SceneMaterial> materials_;
    std::vector<std::string> material_names_;
    std::vector<GpuTexture> textures_;
    std::vector<std::string> texture_names_;
    std::string source_path_ = "builtin";
    std::string document_path_;
    std::filesystem::path gltf_source_path_;
};

} // namespace genesis
