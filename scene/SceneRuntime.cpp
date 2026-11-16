#include "scene/SceneRuntime.hpp"

#include "scene/Frustum.hpp"
#include "scene/MeshBounds.hpp"

#include "animation/AnimationAuthoring.hpp"
#include "animation/AnimationEvaluator.hpp"
#include "assets/GltfLoader.hpp"
#include "assets/PrimitiveMeshes.hpp"
#include "assets/TextureLoader.hpp"
#include "platform/Paths.hpp"
#include "scene/DefaultMaterial.hpp"
#include "scene/ScenePath.hpp"
#include "scene/SceneSerializer.hpp"
#include "scene/GnsPackage.hpp"

#include <algorithm>
#include <cstring>
#include <system_error>
#include <unordered_map>
#include <vector>
#include <glm/gtc/quaternion.hpp>
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace genesis {

SceneRuntime::SceneRuntime(const VulkanContext& context)
    : context_(context), fluid_world_(context) {
    animation_graph_.ensureDefaults();
}

void SceneRuntime::clear() {
    if (context_.device() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(context_.device());
    }
    scene_ = Scene{};
    meshes_.clear();
    mesh_names_.clear();
    materials_.clear();
    material_names_.clear();
    textures_.clear();
    texture_names_.clear();
    source_path_ = "builtin";
    document_path_.clear();
    gltf_source_path_.clear();
    animation_clips_.clear();
    animation_graph_ = {};
    animation_playback_ = {};
    animation_graph_.ensureDefaults();
    particle_world_.clear();
    fluid_world_.clear();
    particle_step_accumulator_ = 0.0;
    particle_cube_mesh_id_.reset();
    particle_sphere_mesh_id_.reset();
    particle_sprite_mesh_id_.reset();
    particle_sprite_texture_id_.reset();
    fluid_box_mesh_id_.reset();
    fluid_display_material_id_.reset();
}

void SceneRuntime::loadGltf(const std::filesystem::path& path) {
    loadGltfFile(resolveAssetPath(path));
}

void SceneRuntime::loadGltfFile(const std::filesystem::path& absolute_path) {
    loadGltfSource(absolute_path);
    document_path_.clear();
}

void SceneRuntime::loadDocument(const std::filesystem::path& path) {
    const SceneDocument document = SceneSerializer::load(path);
    const std::filesystem::path resolved = resolveAssetPath(path);
    const std::filesystem::path asset_root = resolved.parent_path();
    if (document.source == "builtin") {
        buildFromDocument(document);
    } else {
        loadGltfSource(resolveAssetPath(document.source));
        applyDocumentOverrides(document);
    }
    resolveMaterialTextures(asset_root);
    document_path_ = path.string();
}

void SceneRuntime::loadDocumentData(const SceneDocument& document,
                                    const std::filesystem::path& asset_root) {
    if (document.source == "builtin") {
        buildFromDocument(document);
    } else {
        std::filesystem::path gltf_path = document.source;
        if (!gltf_path.is_absolute()) {
            gltf_path = asset_root / gltf_path;
        }
        if (!std::filesystem::exists(gltf_path)) {
            gltf_path = resolveAssetPath(document.source);
        }
        loadGltfSource(gltf_path);
        applyDocumentOverrides(document);
    }
    resolveMaterialTextures(asset_root);
}

void SceneRuntime::loadDocumentFile(const std::filesystem::path& absolute_path) {
    loadDocumentData(SceneSerializer::load(absolute_path), absolute_path.parent_path());
    document_path_ = absolute_path.string();
}

void SceneRuntime::loadGnsFile(const std::filesystem::path& absolute_path) {
    const GnsPackage package = GnsPackage::load(absolute_path);
    loadDocumentData(package.resolveScene(absolute_path.parent_path()), absolute_path.parent_path());
    document_path_ = absolute_path.string();
}

SceneDocument SceneRuntime::buildSceneDocument() const {
    SceneDocument document =
        SceneSerializer::buildDocument(scene_, source_path_, mesh_names_, material_names_, materials_);
    document.animations = animation_clips_;
    for (AnimationClip& clip : document.animations) {
        for (AnimationChannel& channel : clip.channels) {
            if (channel.target_node_id != 0) {
                channel.target_node_name = nodePath(scene_, channel.target_node_id);
            }
            channel.target_node_id = 0;
        }
    }
    return document;
}

void SceneRuntime::saveDocument(const std::filesystem::path& path) {
    SceneDocument document = buildSceneDocument();
    const std::string ext = path.extension().string();
    if (ext == ".gns" || ext == ".GNS" || ext == ".genesis" || ext == ".GENESIS") {
        GnsPackage package{};
        package.version = 1;
        package.name = path.stem().string();
        package.scene = std::move(document);
        package.pixera.streams.push_back(GnsStreamDesc{});
        // Best-effort bindings from common demo/editor names.
        for (const Node& node : scene_.nodes) {
            if (node.mesh_resource.has_value() && package.pixera.bindings.target_node.empty()) {
                package.pixera.bindings.target_node = nodePath(scene_, node.id);
            }
            if (node.light.has_value() && package.pixera.bindings.light_node.empty()) {
                package.pixera.bindings.light_node = nodePath(scene_, node.id);
            }
        }
        GnsPackage::save(package, path);
    } else {
        SceneSerializer::save(document, path);
    }
    document_path_ = path.string();
}

void SceneRuntime::loadDemo() {
    clear();
    scene_.createEmptyNode("Scene");
    scene_.active_output_camera.reset();
    source_path_ = "builtin";
    scene_.updateWorldTransforms();
}

Node* SceneRuntime::findNodeByPath(const std::string& path) {
    if (std::optional<NodeId> id = genesis::findNodeByPath(scene_, path)) {
        return scene_.find(*id);
    }
    return nullptr;
}

const Node* SceneRuntime::findNodeByPath(const std::string& path) const {
    if (std::optional<NodeId> id = genesis::findNodeByPath(scene_, path)) {
        return scene_.find(*id);
    }
    return nullptr;
}

std::uint32_t SceneRuntime::addMesh(std::string name, Mesh mesh) {
    meshes_.push_back(std::move(mesh));
    mesh_names_.push_back(std::move(name));
    return static_cast<std::uint32_t>(meshes_.size() - 1);
}

std::uint32_t SceneRuntime::addMaterial(std::string name, SceneMaterial material) {
    materials_.push_back(std::move(material));
    material_names_.push_back(std::move(name));
    return static_cast<std::uint32_t>(materials_.size() - 1);
}

std::uint32_t SceneRuntime::addDefaultPbrMaterial(std::string name) {
    const std::uint32_t id = addMaterial(std::move(name), makeDefaultPbrMaterial());
    ensureMaterialTexturesLoaded();
    return id;
}

void SceneRuntime::ensureMaterialTexturesLoaded() {
    resolveMaterialTextures(std::filesystem::path("."));
}

std::uint32_t SceneRuntime::addTexture(std::string name, GpuTexture texture) {
    textures_.push_back(std::move(texture));
    texture_names_.push_back(std::move(name));
    return static_cast<std::uint32_t>(textures_.size() - 1);
}

std::int32_t SceneRuntime::addAnimationClip(AnimationClip clip) {
    animation_clips_.push_back(std::move(clip));
    return static_cast<std::int32_t>(animation_clips_.size() - 1);
}

std::int32_t SceneRuntime::ensureSceneAnimationClip() {
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(animation_clips_.size()); ++i) {
        if (animation_clips_[static_cast<std::size_t>(i)].name == "Scene") {
            return i;
        }
    }
    AnimationClip clip{};
    clip.name = "Scene";
    clip.duration_seconds = 5.0f;
    clip.ticks_per_second = 30.0f;
    const std::int32_t index = addAnimationClip(std::move(clip));
    animation_graph_.states.push_back(AnimationGraphState{"Scene", index, true});
    if (animation_graph_.entry_state.empty() || animation_graph_.entry_state == "Idle") {
        animation_graph_.entry_state = "Scene";
        animation_graph_.active_state = "Scene";
    }
    if (animation_playback_.clip_index < 0) {
        animation_playback_.clip_index = index;
    }
    return index;
}

void SceneRuntime::keyframeNode(NodeId node_id, float time_seconds, AnimationTrack tracks) {
    const Node* node = scene_.find(node_id);
    if (!node) {
        return;
    }
    std::int32_t target = animation_playback_.clip_index;
    if (target < 0 || target >= static_cast<std::int32_t>(animation_clips_.size())) {
        target = ensureSceneAnimationClip();
        animation_playback_.clip_index = target;
    }
    genesis::keyframeNode(animation_clips_[static_cast<std::size_t>(target)], *node, time_seconds, tracks);
}

void SceneRuntime::keyframeNodes(const std::vector<NodeId>& node_ids, float time_seconds,
                                 AnimationTrack tracks) {
    for (NodeId id : node_ids) {
        keyframeNode(id, time_seconds, tracks);
    }
}

void SceneRuntime::applyActiveAnimation() {
    if (animation_playback_.clip_index < 0 ||
        animation_playback_.clip_index >= static_cast<std::int32_t>(animation_clips_.size())) {
        return;
    }
    const AnimationClip& clip = animation_clips_[static_cast<std::size_t>(animation_playback_.clip_index)];
    applyAnimationClip(scene_, clip, animation_playback_.time_seconds, animation_playback_.loop);
    updateTransforms();
}

void SceneRuntime::tickAnimations(float dt_seconds) {
    if (animation_playback_.clip_index < 0 ||
        animation_playback_.clip_index >= static_cast<std::int32_t>(animation_clips_.size())) {
        return;
    }
    AnimationClip& clip = animation_clips_[static_cast<std::size_t>(animation_playback_.clip_index)];
    tickAnimationPlayback(animation_playback_, &clip, dt_seconds);
    // Only drive transforms while playing so paused scrubbing / inspector edits can be keyed.
    if (animation_playback_.playing) {
        applyAnimationClip(scene_, clip, animation_playback_.time_seconds, animation_playback_.loop);
    }
}

void SceneRuntime::tickParticles(float dt_seconds, float fixed_step_seconds, std::uint64_t max_steps) {
    if (fixed_step_seconds <= 1e-8f) {
        return;
    }
    particle_step_accumulator_ += static_cast<double>(dt_seconds);
    std::uint64_t steps = 0;
    while (particle_step_accumulator_ >= static_cast<double>(fixed_step_seconds) && steps < max_steps) {
        particle_step_accumulator_ -= static_cast<double>(fixed_step_seconds);
        ++steps;
    }
    if (steps >= max_steps) {
        particle_step_accumulator_ = 0.0;
    }
    tickParticlesSteps(steps, fixed_step_seconds);
}

void SceneRuntime::tickParticlesSteps(const std::uint64_t steps, const float fixed_step_seconds) {
    if (steps == 0 || fixed_step_seconds <= 1e-8f) {
        return;
    }
    particle_world_.simulate(scene_, steps, fixed_step_seconds);
}

void SceneRuntime::simulateFluids(VkCommandBuffer cmd, const float dt_seconds) {
    fluid_world_.simulate(cmd, scene_, dt_seconds);
    particle_world_.setFluidWorld(&fluid_world_);
}

void SceneRuntime::appendFluidDrawCommands(std::vector<SceneDrawCommand>& out) {
    if (!fluid_box_mesh_id_.has_value()) {
        fluid_box_mesh_id_ = addMesh("FluidBox", createFluidDisplayBoxMesh(context_));
    }
    if (!fluid_display_material_id_.has_value()) {
        SceneMaterial mat{};
        mat.base_color = glm::vec4(1.0f);
        mat.metallic = 0.0f;
        mat.roughness = 1.0f;
        mat.emissive = glm::vec3(0.0f);
        mat.emissive_strength = 0.0f;
        mat.alpha_mode = AlphaMode::Volume;
        fluid_display_material_id_ = addMaterial("FluidDisplay", mat);
    }

    const Mesh* mesh = this->mesh(*fluid_box_mesh_id_);
    if (!mesh) {
        return;
    }

    for (const FluidDrawItem& item : fluid_world_.drawItems()) {
        if (!item.display_texture) {
            continue;
        }
        SceneDrawCommand cmd{};
        cmd.node_id = item.node_id;
        cmd.mesh = mesh;
        cmd.model = item.model;
        cmd.base_color_texture = item.display_texture;
        cmd.material = makeMeshMaterialPushConstants(*material(*fluid_display_material_id_));
        // Dye tint for volumetric accumulation.
        cmd.material.base_color[0] = item.dye_color.r;
        cmd.material.base_color[1] = item.dye_color.g;
        cmd.material.base_color[2] = item.dye_color.b;
        cmd.material.base_color[3] = item.dye_color.a;
        // Force Volume mode every draw (material template may be shared/cached).
        cmd.material.material_params[2] = static_cast<float>(static_cast<int>(AlphaMode::Volume));
        // Grid resolution + atlas tiles for the raymarcher.
        cmd.material.material_params[3] = static_cast<float>(std::max(item.grid_resolution, 8u));
        cmd.material.emissive[3] = static_cast<float>(std::max(item.atlas_tiles_x, 1u));
        cmd.instance_color = glm::vec4(1.0f);
        out.push_back(cmd);
    }
}

void SceneRuntime::resetParticles() {
    particle_world_.reset();
    particle_step_accumulator_ = 0.0;
}

void SceneRuntime::setParticleViewBasis(const glm::vec3& camera_right, const glm::vec3& camera_up) {
    particle_view_basis_.right = camera_right;
    particle_view_basis_.up = camera_up;
    particle_view_basis_.valid = true;
}

void SceneRuntime::appendParticleDrawCommands(std::vector<SceneDrawCommand>& out) {
    if (!particle_cube_mesh_id_.has_value()) {
        particle_cube_mesh_id_ = addMesh("ParticleCube", createCubeMesh(context_));
    }
    if (!particle_sphere_mesh_id_.has_value()) {
        // Low-poly sphere for mesh particles (efficiency vs 12×8).
        particle_sphere_mesh_id_ = addMesh("ParticleSphere", createSphereMesh(context_, 0.5f, 8, 6));
    }
    if (!particle_sprite_mesh_id_.has_value()) {
        // XY quad (normal +Z) — matches billboard basis in ParticleWorld::collectInstances.
        // Floor/XZ planes render edge-on under that transform; do not use createPlaneMesh here.
        particle_sprite_mesh_id_ =
            addMesh("ParticleSprite", createFluidDisplayQuadMesh(context_, 0.5f));
    }
    if (!particle_sprite_texture_id_.has_value()) {
        // Soft white circle + feathered alpha (Unreal default sprite look).
        particle_sprite_texture_id_ =
            addTexture("ParticleSoftCircle", GpuTexture::createSoftCircleSprite(context_, 64));
    }

    if (!particle_view_basis_.valid) {
        if (const Node* cam = scene_.activeOutputCameraNode()) {
            const glm::mat3 rot = glm::mat3(cam->world);
            particle_view_basis_.right = glm::normalize(glm::vec3(rot[0]));
            particle_view_basis_.up = glm::normalize(glm::vec3(rot[1]));
            particle_view_basis_.valid = true;
        }
    }

    std::vector<ParticleWorld::ParticleInstanceBatch> batches;
    particle_world_.collectInstances(scene_, batches, particle_view_basis_);

    SceneMaterial lit{};
    lit.base_color = glm::vec4(1.0f);
    lit.roughness = 0.55f;
    lit.metallic = 0.0f;
    lit.alpha_mode = AlphaMode::Blend;

    SceneMaterial unlit = lit;
    unlit.emissive = glm::vec3(1.0f);
    unlit.emissive_strength = 1.0f;
    unlit.roughness = 1.0f;

    const GpuTexture* soft_circle = texture(*particle_sprite_texture_id_);

    for (const ParticleWorld::ParticleInstanceBatch& batch : batches) {
        const Mesh* mesh = nullptr;
        const bool is_sprite = batch.mesh == ParticleRenderMesh::Sprite;
        switch (batch.mesh) {
        case ParticleRenderMesh::Sphere:
            mesh = this->mesh(*particle_sphere_mesh_id_);
            break;
        case ParticleRenderMesh::Sprite:
            mesh = this->mesh(*particle_sprite_mesh_id_);
            break;
        case ParticleRenderMesh::Cube:
        default:
            mesh = this->mesh(*particle_cube_mesh_id_);
            break;
        }
        if (!mesh) {
            continue;
        }
        const SceneMaterial& material = batch.unlit ? unlit : lit;
        // One SceneDrawCommand per instance; buildDrawBatches GPU-instances them.
        out.reserve(out.size() + batch.instances.size());
        for (const GpuMeshInstance& instance : batch.instances) {
            SceneDrawCommand cmd{};
            cmd.node_id = 0;
            cmd.mesh = mesh;
            cmd.model = instance.model;
            cmd.material = makeMeshMaterialPushConstants(material);
            cmd.instance_color = instance.color;
            cmd.additive = batch.additive;
            if (is_sprite) {
                cmd.base_color_texture = soft_circle;
            }
            out.push_back(cmd);
        }
    }
}

Mesh* SceneRuntime::mesh(const std::uint32_t id) {
    return id < meshes_.size() ? &meshes_[id] : nullptr;
}

const Mesh* SceneRuntime::mesh(const std::uint32_t id) const {
    return id < meshes_.size() ? &meshes_[id] : nullptr;
}

SceneMaterial* SceneRuntime::material(const std::uint32_t id) {
    return id < materials_.size() ? &materials_[id] : nullptr;
}

const SceneMaterial* SceneRuntime::material(const std::uint32_t id) const {
    return id < materials_.size() ? &materials_[id] : nullptr;
}

GpuTexture* SceneRuntime::texture(const std::uint32_t id) {
    return id < textures_.size() ? &textures_[id] : nullptr;
}

const GpuTexture* SceneRuntime::texture(const std::uint32_t id) const {
    return id < textures_.size() ? &textures_[id] : nullptr;
}

const SceneMaterial& SceneRuntime::resolveMaterial(const Node& node) const {
    static const SceneMaterial kDefault{};
    if (!node.material_resource.has_value() || *node.material_resource >= materials_.size()) {
        return kDefault;
    }
    return materials_[*node.material_resource];
}

void SceneRuntime::updateTransforms() {
    scene_.updateWorldTransforms();
}

void SceneRuntime::collectDrawCommands(std::vector<SceneDrawCommand>& out,
                                       SceneRenderStats* out_stats,
                                       const SceneDrawCollectParams& params) const {
    out.clear();
    out.reserve(scene_.nodes.size());

    SceneRenderStats stats{};
    stats.scene_nodes = static_cast<std::uint32_t>(scene_.nodes.size());

    std::optional<Frustum> frustum;
    if (params.enable_cpu_culling && params.view_proj_col_major != nullptr) {
        glm::mat4 view_proj{};
        std::memcpy(&view_proj[0][0], params.view_proj_col_major, sizeof(float) * 16);
        frustum = frustumFromViewProjection(view_proj);
    }

    for (const Node& node : scene_.nodes) {
        if (node.light.has_value() && node.light->enabled && node.visible) {
            ++stats.active_lights;
        }
        if (!node.visible || !node.mesh_resource.has_value()) continue;
        const std::uint32_t mesh_id = *node.mesh_resource;
        if (mesh_id >= meshes_.size()) continue;

        if (frustum.has_value()) {
            if (!isMeshVisibleInFrustum(*frustum, node.world, meshes_[mesh_id].local_aabb_min,
                                        meshes_[mesh_id].local_aabb_max)) {
                ++stats.culled_mesh_nodes;
                continue;
            }
        }

        SceneDrawCommand cmd{};
        cmd.node_id = node.id;
        cmd.mesh = &meshes_[mesh_id];
        cmd.model = node.world;
        const SceneMaterial& material = resolveMaterial(node);
        cmd.material = makeMeshMaterialPushConstants(material);
        if (material.base_color_texture_resource.has_value()) {
            cmd.base_color_texture = texture(*material.base_color_texture_resource);
        }
        if (material.metallic_roughness_texture_resource.has_value()) {
            cmd.metallic_roughness_texture = texture(*material.metallic_roughness_texture_resource);
        }
        if (material.normal_texture_resource.has_value()) {
            cmd.normal_texture = texture(*material.normal_texture_resource);
        }
        if (material.emissive_texture_resource.has_value()) {
            cmd.emissive_texture = texture(*material.emissive_texture_resource);
        }
        out.push_back(cmd);

        ++stats.visible_mesh_nodes;
        stats.triangles += cmd.mesh->index_count / 3;
    }

    stats.draw_calls = static_cast<std::uint32_t>(out.size());
    if (out_stats) {
        *out_stats = stats;
    }
}

std::optional<glm::mat4> SceneRuntime::outputCameraViewProjection(float aspect) const {
    const Node* cam_node = scene_.activeOutputCameraNode();
    if (!cam_node || !cam_node->camera.has_value()) return std::nullopt;
    return cam_node->camera->viewProjection(cam_node->world, aspect);
}

std::optional<glm::vec3> SceneRuntime::outputCameraPosition() const {
    const Node* cam_node = scene_.activeOutputCameraNode();
    if (!cam_node) return std::nullopt;
    return glm::vec3(cam_node->world[3]);
}

std::optional<ScenePickResult> SceneRuntime::pickNodeAtRay(const PickRay& ray,
                                                           const ScenePickParams& params) const {
    return genesis::pickNodeAtRay(scene_, meshes_, ray, params);
}

void SceneRuntime::loadGltfSource(const std::filesystem::path& path) {
    if (context_.device() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(context_.device());
    }
    materials_.clear();
    material_names_.clear();
    textures_.clear();
    texture_names_.clear();
    scene_ = GltfLoader::loadScene(path, &materials_);
    meshes_.clear();
    mesh_names_.clear();
    particle_cube_mesh_id_.reset();
    particle_sphere_mesh_id_.reset();
    particle_sprite_mesh_id_.reset();
    particle_sprite_texture_id_.reset();
    fluid_box_mesh_id_.reset();
    fluid_display_material_id_.reset();

    for (std::size_t i = 0; i < materials_.size(); ++i) {
        material_names_.push_back("Material " + std::to_string(i));
    }

    GltfLoader::loadTexturesForFile(path, context_, materials_, textures_, texture_names_);

    const std::uint32_t imported_id = addMesh("ImportedMesh", GltfLoader::loadFirstMesh(context_, path));

    for (Node& node : scene_.nodes) {
        if (node.has_mesh) {
            node.mesh_resource = imported_id;
            if (!node.material_resource.has_value()) {
                node.has_material = true;
                node.material_resource = 0;
            }
        }
    }

    source_path_ = path.string();
    gltf_source_path_ = path;
    scene_.active_output_camera.reset();
    scene_.updateWorldTransforms();
}

void SceneRuntime::applyDocumentOverrides(const SceneDocument& document) {
    materials_.clear();
    material_names_.clear();
    for (const auto& [name, material] : document.materials) {
        addMaterial(name, material);
    }

    for (const SceneDocumentNode& entry : document.nodes) {
        if (std::optional<NodeId> id = genesis::findNodeByPath(scene_, entry.path)) {
            applyNodeEntry(*id, entry);
        } else {
            createNodeFromEntry(entry);
        }
    }

    finishDocumentLoad(document);
}

void SceneRuntime::buildFromDocument(const SceneDocument& document) {
    clear();
    source_path_ = "builtin";

    for (const auto& [name, material] : document.materials) {
        addMaterial(name, material);
    }

    std::vector<SceneDocumentNode> nodes = document.nodes;
    std::sort(nodes.begin(), nodes.end(), [](const SceneDocumentNode& a, const SceneDocumentNode& b) {
        const auto depth = [](const std::string& path) {
            return std::count(path.begin(), path.end(), '/') + 1;
        };
        return depth(a.path) < depth(b.path);
    });

    for (const SceneDocumentNode& entry : nodes) {
        createNodeFromEntry(entry);
    }

    finishDocumentLoad(document);
}

void SceneRuntime::applyNodeEntry(const NodeId id, const SceneDocumentNode& entry) {
    Node* node = scene_.find(id);
    if (!node) return;

    node->name = entry.name;
    node->local = entry.local;
    node->visible = entry.visible;
    node->pickable = entry.pickable;

    if (entry.mesh_ref.has_value()) {
        node->has_mesh = true;
        node->mesh_resource = resolveMeshRef(*entry.mesh_ref);
    }
    if (entry.material_ref.has_value()) {
        node->has_material = true;
        node->material_resource = resolveMaterialRef(*entry.material_ref);
    }
    node->camera = entry.camera;
    node->has_camera = entry.camera.has_value();
    node->light = entry.light;
    node->has_light = entry.light.has_value();
    node->particle_emitter = entry.particle_emitter;
    node->has_particle_emitter = entry.particle_emitter.has_value();
    node->particle_fluid = entry.particle_fluid;
    node->has_particle_fluid = entry.particle_fluid.has_value();
}

void SceneRuntime::createNodeFromEntry(const SceneDocumentNode& entry) {
    std::optional<NodeId> parent_id;
    if (entry.parent_path.has_value()) {
        parent_id = genesis::findNodeByPath(scene_, *entry.parent_path);
    }

    NodeId id = 0;
    if (entry.mesh_ref.has_value()) {
        const std::uint32_t mesh_id = resolveMeshRef(*entry.mesh_ref);
        std::optional<std::uint32_t> material_id;
        if (entry.material_ref.has_value()) {
            material_id = resolveMaterialRef(*entry.material_ref);
        }
        id = scene_.createMeshNode(entry.name, mesh_id, parent_id, material_id);
    } else if (entry.camera.has_value()) {
        id = scene_.createCameraNode(entry.name, parent_id);
    } else if (entry.light.has_value()) {
        id = scene_.createLightNode(entry.name, entry.light->type, parent_id);
    } else if (entry.particle_emitter.has_value()) {
        id = scene_.createParticleEmitterNode(entry.name, parent_id, *entry.particle_emitter);
    } else if (entry.particle_fluid.has_value()) {
        id = scene_.createFluidVolumeNode(entry.name, parent_id, *entry.particle_fluid);
    } else {
        id = scene_.createEmptyNode(entry.name, parent_id);
    }

    applyNodeEntry(id, entry);
}

void SceneRuntime::finishDocumentLoad(const SceneDocument& document) {
    scene_.updateWorldTransforms();

    if (document.active_output_camera.has_value()) {
        if (std::optional<NodeId> cam_id = genesis::findNodeByPath(scene_, *document.active_output_camera)) {
            scene_.setActiveOutputCamera(*cam_id);
        } else {
            scene_.active_output_camera.reset();
        }
    } else {
        scene_.active_output_camera.reset();
    }

    animation_clips_ = document.animations;
    for (AnimationClip& clip : animation_clips_) {
        bindClipChannels(scene_, clip);
    }
    animation_graph_ = {};
    animation_graph_.ensureDefaults();
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(animation_clips_.size()); ++i) {
        animation_graph_.states.push_back(
            AnimationGraphState{animation_clips_[static_cast<std::size_t>(i)].name, i, true});
    }
    if (!animation_clips_.empty()) {
        animation_graph_.entry_state = animation_clips_.front().name;
        animation_graph_.active_state = animation_graph_.entry_state;
        animation_playback_.clip_index = 0;
    } else {
        animation_playback_ = {};
    }

    particle_world_.reset();
    particle_step_accumulator_ = 0.0;
}

std::uint32_t SceneRuntime::resolveMeshRef(const std::string& ref) {
    for (std::size_t i = 0; i < mesh_names_.size(); ++i) {
        if (mesh_names_[i] == ref) return static_cast<std::uint32_t>(i);
    }

    if (ref == "Cube") {
        return addMesh(ref, createCubeMesh(context_));
    }
    if (ref == "Sphere") {
        return addMesh(ref, createSphereMesh(context_));
    }
    if (ref == "Cylinder") {
        return addMesh(ref, createCylinderMesh(context_));
    }
    if (ref == "Plane") {
        return addMesh(ref, createPlaneMesh(context_));
    }
    if (ref == "GroundPlane") {
        return addMesh(ref, createPlaneMesh(context_, 4.0f, 8.0f));
    }
    if (ref == "ImportedMesh" && !gltf_source_path_.empty()) {
        return addMesh(ref, GltfLoader::loadFirstMesh(context_, gltf_source_path_));
    }

    return addMesh(ref, createCubeMesh(context_));
}

std::uint32_t SceneRuntime::resolveMaterialRef(const std::string& ref) {
    for (std::size_t i = 0; i < material_names_.size(); ++i) {
        if (material_names_[i] == ref) return static_cast<std::uint32_t>(i);
    }
    if (materials_.empty()) {
        return addDefaultPbrMaterial(ref);
    }
    return 0;
}

void SceneRuntime::resolveMaterialTextures(const std::filesystem::path& asset_root) {
    std::unordered_map<std::string, std::uint32_t> loaded_by_path;

    auto find_texture = [&](const std::string& ref) -> std::filesystem::path {
        if (ref.empty()) {
            return {};
        }
        const std::filesystem::path as_path(ref);
        std::vector<std::filesystem::path> candidates = {
            as_path,
            asset_root / as_path,
            asset_root / ".." / as_path,
            asset_root / ".." / "textures" / as_path,
            asset_root / "textures" / as_path,
            std::filesystem::path("assets") / as_path,
            std::filesystem::path("assets/textures") / as_path,
            std::filesystem::path("build/assets/textures") / as_path,
            std::filesystem::path("build/assets") / as_path,
        };
        for (const std::filesystem::path& candidate : candidates) {
            std::error_code ec;
            if (!candidate.empty() && std::filesystem::exists(candidate, ec) && !ec) {
                return std::filesystem::absolute(candidate);
            }
        }
        // Last resort: resolveAssetPath may search configured roots (throws if missing).
        try {
            return resolveAssetPath(std::filesystem::path("assets/textures") / as_path);
        } catch (const std::exception&) {
        }
        try {
            return resolveAssetPath(as_path);
        } catch (const std::exception&) {
        }
        return {};
    };

    auto load_slot = [&](std::string& name, std::optional<std::uint32_t>& resource, bool color_map) {
        if (name.empty() || resource.has_value()) {
            return;
        }
        const std::filesystem::path path = find_texture(name);
        if (path.empty()) {
            spdlog::warn("Material texture not found: {}", name);
            return;
        }
        const std::string key = path.string();
        if (const auto it = loaded_by_path.find(key); it != loaded_by_path.end()) {
            resource = it->second;
            name = path.filename().string();
            return;
        }
        for (std::size_t i = 0; i < texture_names_.size(); ++i) {
            if (texture_names_[i] == key) {
                resource = static_cast<std::uint32_t>(i);
                loaded_by_path[key] = *resource;
                name = path.filename().string();
                return;
            }
        }
        try {
            auto loaded = loadImageTexture(context_, path, TextureColorSpace::Auto, color_map);
            resource = addTexture(key, std::move(loaded.texture));
            loaded_by_path[key] = *resource;
            name = path.filename().string();
            spdlog::info("Loaded material texture {} -> {}", path.string(), *resource);
        } catch (const std::exception& ex) {
            spdlog::error("Failed to load material texture {}: {}", path.string(), ex.what());
        }
    };

    for (SceneMaterial& material : materials_) {
        load_slot(material.base_color_texture, material.base_color_texture_resource, true);
        load_slot(material.metallic_roughness_texture, material.metallic_roughness_texture_resource,
                  false);
        load_slot(material.normal_texture, material.normal_texture_resource, false);
        load_slot(material.emissive_texture, material.emissive_texture_resource, true);
    }
}

} // namespace genesis
