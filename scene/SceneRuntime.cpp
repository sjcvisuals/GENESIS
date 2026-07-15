#include "scene/SceneRuntime.hpp"

#include "assets/GltfLoader.hpp"
#include "assets/PrimitiveMeshes.hpp"
#include "platform/Paths.hpp"
#include "scene/ScenePath.hpp"
#include "scene/SceneSerializer.hpp"
#include "scene/GnsPackage.hpp"

#include <algorithm>
#include <glm/gtc/quaternion.hpp>
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace genesis {

SceneRuntime::SceneRuntime(const VulkanContext& context) : context_(context) {}

void SceneRuntime::clear() {
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
    if (document.source == "builtin") {
        buildFromDocument(document);
    } else {
        loadGltfSource(resolveAssetPath(document.source));
        applyDocumentOverrides(document);
    }
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

void SceneRuntime::saveDocument(const std::filesystem::path& path) {
    const SceneDocument document =
        SceneSerializer::buildDocument(scene_, source_path_, mesh_names_, material_names_, materials_);
    SceneSerializer::save(document, path);
    document_path_ = path.string();
}

void SceneRuntime::loadDemo() {
    clear();

    const NodeId root = scene_.createEmptyNode("Demo Scene");

    const NodeId dir_light =
        scene_.createLightNode("Directional Light", SceneLightType::Directional, root);
    if (Node* n = scene_.find(dir_light)) {
        n->local.rotation =
            glm::angleAxis(glm::radians(-50.0f), glm::vec3(1.0f, 0.0f, 0.0f)) *
            glm::angleAxis(glm::radians(40.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        n->light->color = glm::vec3(1.0f, 0.98f, 0.94f);
        n->light->intensity = 1.0f;
    }

    const NodeId camera = scene_.createCameraNode("Main Camera", root);
    if (Node* n = scene_.find(camera)) {
        n->local.position = glm::vec3(0.0f, 2.0f, 8.0f);
        n->local.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        n->camera->fov_y_radians = glm::radians(55.0f);
    }

    scene_.setActiveOutputCamera(camera);
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

std::uint32_t SceneRuntime::addTexture(std::string name, GpuTexture texture) {
    textures_.push_back(std::move(texture));
    texture_names_.push_back(std::move(name));
    return static_cast<std::uint32_t>(textures_.size() - 1);
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
                                       SceneRenderStats* out_stats) const {
    out.clear();
    out.reserve(scene_.nodes.size());

    SceneRenderStats stats{};
    stats.scene_nodes = static_cast<std::uint32_t>(scene_.nodes.size());

    for (const Node& node : scene_.nodes) {
        if (node.light.has_value() && node.light->enabled && node.visible) {
            ++stats.active_lights;
        }
        if (!node.visible || !node.mesh_resource.has_value()) continue;
        const std::uint32_t mesh_id = *node.mesh_resource;
        if (mesh_id >= meshes_.size()) continue;

        SceneDrawCommand cmd{};
        cmd.node_id = node.id;
        cmd.mesh = &meshes_[mesh_id];
        const SceneMaterial& material = resolveMaterial(node);
        cmd.constants = makeMeshDrawPushConstants(&node.world[0][0], material);
        if (material.base_color_texture_resource.has_value()) {
            cmd.base_color_texture = texture(*material.base_color_texture_resource);
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

void SceneRuntime::loadGltfSource(const std::filesystem::path& path) {
    materials_.clear();
    material_names_.clear();
    textures_.clear();
    texture_names_.clear();
    scene_ = GltfLoader::loadScene(path, &materials_);
    meshes_.clear();
    mesh_names_.clear();

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
        return addMaterial(ref, SceneMaterial{});
    }
    return 0;
}

} // namespace genesis
