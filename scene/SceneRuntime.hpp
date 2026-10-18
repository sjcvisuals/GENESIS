#pragma once

#include "animation/AnimationClip.hpp"
#include "animation/AnimationGraph.hpp"
#include "particles/FluidWorld.hpp"
#include "particles/ParticleWorld.hpp"
#include "renderer/Mesh.hpp"
#include "renderer/MeshDrawPushConstants.hpp"
#include "renderer/vulkan/Texture.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "scene/Scene.hpp"
#include "scene/SceneDocument.hpp"
#include "scene/ScenePicker.hpp"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <deque>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace genesis {

struct SceneDrawCommand {
    NodeId node_id = 0;
    const Mesh* mesh = nullptr;
    const GpuTexture* base_color_texture = nullptr;
    const GpuTexture* metallic_roughness_texture = nullptr;
    const GpuTexture* normal_texture = nullptr;
    const GpuTexture* emissive_texture = nullptr;
    glm::mat4 model{1.0f};
    MeshMaterialPushConstants material{};
    glm::vec4 instance_color{1.0f};
};

struct SceneDrawCollectParams {
    const float* view_proj_col_major = nullptr;
    bool enable_cpu_culling = true;
    bool enable_gpu_culling = true;
};

struct SceneRenderStats {
    std::uint32_t draw_calls = 0;
    std::uint32_t draw_batches = 0;
    std::uint32_t triangles = 0;
    std::uint32_t visible_mesh_nodes = 0;
    std::uint32_t culled_mesh_nodes = 0;
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
    SceneDocument buildSceneDocument() const;
    void loadDemo();

    const VulkanContext& context() const { return context_; }

    Scene& scene() { return scene_; }
    const Scene& scene() const { return scene_; }

    Node* findNode(NodeId id) { return scene_.find(id); }
    const Node* findNode(NodeId id) const { return scene_.find(id); }
    Node* findNodeByPath(const std::string& path);
    const Node* findNodeByPath(const std::string& path) const;

    std::uint32_t addMesh(std::string name, Mesh mesh);
    std::uint32_t addMaterial(std::string name, SceneMaterial material);
    // Adds makeDefaultPbrMaterial() and loads its maps from assets/textures/default_pbr/.
    std::uint32_t addDefaultPbrMaterial(std::string name = "Default PBR");
    std::uint32_t addTexture(std::string name, GpuTexture texture);
    // Resolve any material texture paths that are not yet bound to GPU resources.
    void ensureMaterialTexturesLoaded();

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
                             SceneRenderStats* out_stats = nullptr,
                             const SceneDrawCollectParams& params = {}) const;

    std::optional<glm::mat4> outputCameraViewProjection(float aspect) const;
    std::optional<glm::vec3> outputCameraPosition() const;

    std::optional<ScenePickResult> pickNodeAtRay(const PickRay& ray,
                                                 const ScenePickParams& params = {}) const;

    const std::string& sourcePath() const { return source_path_; }
    const std::string& documentPath() const { return document_path_; }
    const std::vector<std::string>& meshNames() const { return mesh_names_; }
    const std::vector<std::string>& materialNames() const { return material_names_; }
    // deque: push_back does not invalidate pointers to existing elements (bindless / draw cmds).
    const std::deque<Mesh>& meshes() const { return meshes_; }
    const std::vector<SceneMaterial>& materials() const { return materials_; }
    const std::deque<GpuTexture>& textures() const { return textures_; }
    const std::vector<std::string>& textureNames() const { return texture_names_; }

    std::int32_t addAnimationClip(AnimationClip clip);
    // Creates or returns the editable "Scene" clip used for in-editor keyframes.
    std::int32_t ensureSceneAnimationClip();
    void keyframeNode(NodeId node_id, float time_seconds,
                      AnimationTrack tracks = AnimationTrack::Transform);
    void keyframeNodes(const std::vector<NodeId>& node_ids, float time_seconds,
                       AnimationTrack tracks = AnimationTrack::Transform);
    const std::vector<AnimationClip>& animationClips() const { return animation_clips_; }
    std::vector<AnimationClip>& animationClips() { return animation_clips_; }
    AnimationGraph& animationGraph() { return animation_graph_; }
    const AnimationGraph& animationGraph() const { return animation_graph_; }
    AnimationPlayback& animationPlayback() { return animation_playback_; }
    const AnimationPlayback& animationPlayback() const { return animation_playback_; }
    void tickAnimations(float dt_seconds);
    void applyActiveAnimation();

    // Fixed-step particle simulation (dt accumulated into 0.01s steps by default).
    void tickParticles(float dt_seconds, float fixed_step_seconds = 0.01f,
                       std::uint64_t max_steps = 8);
    void tickParticlesSteps(std::uint64_t steps, float fixed_step_seconds);
    void resetParticles();
    ParticleWorld& particleWorld() { return particle_world_; }
    const ParticleWorld& particleWorld() const { return particle_world_; }
    void setParticleViewBasis(const glm::vec3& camera_right, const glm::vec3& camera_up);
    void appendParticleDrawCommands(std::vector<SceneDrawCommand>& out);

    FluidWorld& fluidWorld() { return fluid_world_; }
    const FluidWorld& fluidWorld() const { return fluid_world_; }
    void simulateFluids(VkCommandBuffer cmd, float dt_seconds);
    void appendFluidDrawCommands(std::vector<SceneDrawCommand>& out);

private:
    void loadGltfSource(const std::filesystem::path& path);
    void applyDocumentOverrides(const SceneDocument& document);
    void buildFromDocument(const SceneDocument& document);
    void applyNodeEntry(NodeId id, const SceneDocumentNode& entry);
    void createNodeFromEntry(const SceneDocumentNode& entry);
    void finishDocumentLoad(const SceneDocument& document);
    void resolveMaterialTextures(const std::filesystem::path& asset_root);

    const VulkanContext& context_;
    Scene scene_{};
    std::deque<Mesh> meshes_;
    std::vector<std::string> mesh_names_;
    std::vector<SceneMaterial> materials_;
    std::vector<std::string> material_names_;
    std::deque<GpuTexture> textures_;
    std::vector<std::string> texture_names_;
    std::string source_path_ = "builtin";
    std::string document_path_;
    std::filesystem::path gltf_source_path_;
    std::vector<AnimationClip> animation_clips_;
    AnimationGraph animation_graph_{};
    AnimationPlayback animation_playback_{};
    ParticleWorld particle_world_{};
    FluidWorld fluid_world_;
    double particle_step_accumulator_ = 0.0;
    std::optional<std::uint32_t> particle_cube_mesh_id_;
    std::optional<std::uint32_t> particle_sphere_mesh_id_;
    std::optional<std::uint32_t> particle_sprite_mesh_id_;
    std::optional<std::uint32_t> particle_sprite_texture_id_;
    ParticleViewBasis particle_view_basis_{};
    std::optional<std::uint32_t> fluid_box_mesh_id_;
    std::optional<std::uint32_t> fluid_display_material_id_;
};

} // namespace genesis
