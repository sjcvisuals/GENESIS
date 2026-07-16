#include "assets/ModelImporter.hpp"

#include "assets/MeshUpload.hpp"
#include "assets/PrimitiveMeshes.hpp"
#include "assets/TextureLoader.hpp"
#include "animation/AnimationClip.hpp"
#include "animation/AnimationGraph.hpp"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <glm/gtc/quaternion.hpp>
#include <spdlog/spdlog.h>

#include <cctype>
#include <unordered_map>

namespace genesis {

namespace {

std::string toLower(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return value;
}

void decomposeAiMatrix(const aiMatrix4x4& m, Transform& out) {
    aiVector3D scaling;
    aiQuaternion rotation;
    aiVector3D position;
    m.Decompose(scaling, rotation, position);
    out.position = glm::vec3(position.x, position.y, position.z);
    out.rotation = glm::normalize(glm::quat(rotation.w, rotation.x, rotation.y, rotation.z));
    out.scale = glm::vec3(scaling.x, scaling.y, scaling.z);
}

Mesh uploadAiMesh(const VulkanContext& context, const aiMesh& mesh) {
    std::vector<MeshVertexPNU> vertices;
    std::vector<std::uint32_t> indices;
    vertices.reserve(mesh.mNumVertices);
    indices.reserve(mesh.mNumFaces * 3);

    for (unsigned i = 0; i < mesh.mNumVertices; ++i) {
        MeshVertexPNU v{};
        v.px = mesh.mVertices[i].x;
        v.py = mesh.mVertices[i].y;
        v.pz = mesh.mVertices[i].z;
        if (mesh.HasNormals()) {
            v.nx = mesh.mNormals[i].x;
            v.ny = mesh.mNormals[i].y;
            v.nz = mesh.mNormals[i].z;
        } else {
            v.nx = 0.0f;
            v.ny = 1.0f;
            v.nz = 0.0f;
        }
        if (mesh.HasTextureCoords(0)) {
            v.u = mesh.mTextureCoords[0][i].x;
            v.v = mesh.mTextureCoords[0][i].y;
        }
        vertices.push_back(v);
    }

    for (unsigned f = 0; f < mesh.mNumFaces; ++f) {
        const aiFace& face = mesh.mFaces[f];
        if (face.mNumIndices != 3) {
            continue;
        }
        indices.push_back(face.mIndices[0]);
        indices.push_back(face.mIndices[1]);
        indices.push_back(face.mIndices[2]);
    }

    return uploadMesh(context, vertices, indices);
}

SceneMaterial materialFromAi(const aiMaterial& mat, const std::filesystem::path& model_dir,
                             SceneRuntime& runtime) {
    SceneMaterial out{};
    aiColor4D color{};
    if (mat.Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS) {
        out.base_color = glm::vec4(color.r, color.g, color.b, color.a);
    }
    if (mat.Get(AI_MATKEY_COLOR_EMISSIVE, color) == AI_SUCCESS) {
        out.emissive = glm::vec3(color.r, color.g, color.b);
    }
    float roughness = 1.0f;
    float metallic = 0.0f;
    if (mat.Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS) {
        out.roughness = roughness;
    }
    if (mat.Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS) {
        out.metallic = metallic;
    }

    aiString tex_path;
    if (mat.GetTexture(aiTextureType_DIFFUSE, 0, &tex_path) == AI_SUCCESS ||
        mat.GetTexture(aiTextureType_BASE_COLOR, 0, &tex_path) == AI_SUCCESS) {
        const std::filesystem::path full = model_dir / tex_path.C_Str();
        try {
            auto loaded = loadImageTexture(runtime.context(), full, TextureColorSpace::sRGB, true);
            const std::uint32_t id = runtime.addTexture(loaded.label, std::move(loaded.texture));
            out.base_color_texture = loaded.label;
            out.base_color_texture_resource = id;
        } catch (const std::exception& ex) {
            spdlog::warn("ModelImporter: failed texture '{}': {}", full.string(), ex.what());
        }
    }
    return out;
}

AnimationClip clipFromAi(const aiAnimation& anim) {
    AnimationClip clip{};
    clip.name = anim.mName.length > 0 ? anim.mName.C_Str() : "Animation";
    clip.ticks_per_second = anim.mTicksPerSecond > 1e-5 ? static_cast<float>(anim.mTicksPerSecond) : 25.0f;
    clip.duration_seconds = static_cast<float>(anim.mDuration / clip.ticks_per_second);

    clip.channels.reserve(anim.mNumChannels);
    for (unsigned c = 0; c < anim.mNumChannels; ++c) {
        const aiNodeAnim* channel = anim.mChannels[c];
        if (!channel) {
            continue;
        }
        AnimationChannel out{};
        out.target_node_name = channel->mNodeName.C_Str();

        out.translations.reserve(channel->mNumPositionKeys);
        for (unsigned i = 0; i < channel->mNumPositionKeys; ++i) {
            const aiVectorKey& key = channel->mPositionKeys[i];
            out.translations.push_back(
                {static_cast<float>(key.mTime / clip.ticks_per_second),
                 glm::vec3(key.mValue.x, key.mValue.y, key.mValue.z)});
        }

        out.rotations.reserve(channel->mNumRotationKeys);
        for (unsigned i = 0; i < channel->mNumRotationKeys; ++i) {
            const aiQuatKey& key = channel->mRotationKeys[i];
            out.rotations.push_back(
                {static_cast<float>(key.mTime / clip.ticks_per_second),
                 glm::normalize(glm::quat(key.mValue.w, key.mValue.x, key.mValue.y, key.mValue.z))});
        }

        out.scales.reserve(channel->mNumScalingKeys);
        for (unsigned i = 0; i < channel->mNumScalingKeys; ++i) {
            const aiVectorKey& key = channel->mScalingKeys[i];
            out.scales.push_back({static_cast<float>(key.mTime / clip.ticks_per_second),
                                  glm::vec3(key.mValue.x, key.mValue.y, key.mValue.z)});
        }

        clip.channels.push_back(std::move(out));
    }
    return clip;
}

NodeId importAiNode(SceneRuntime& runtime, const aiScene& scene, const aiNode& node,
                    std::optional<NodeId> parent, const std::vector<std::uint32_t>& mesh_ids,
                    const std::vector<std::uint32_t>& material_ids,
                    const std::unordered_map<unsigned, std::uint32_t>& mesh_material) {
    const std::string name = node.mName.length > 0 ? node.mName.C_Str() : "Node";
    NodeId id = 0;

    if (node.mNumMeshes > 0) {
        const unsigned ai_mesh_index = node.mMeshes[0];
        const std::uint32_t mesh_id = mesh_ids[ai_mesh_index];
        std::uint32_t material_id = 0;
        if (const auto it = mesh_material.find(ai_mesh_index); it != mesh_material.end()) {
            material_id = it->second;
        } else if (!material_ids.empty()) {
            material_id = material_ids.front();
        }
        id = runtime.scene().createMeshNode(name, mesh_id, parent, material_id);

        // Extra meshes on the same node become child mesh nodes.
        for (unsigned i = 1; i < node.mNumMeshes; ++i) {
            const unsigned extra = node.mMeshes[i];
            std::uint32_t extra_mat = material_id;
            if (const auto it = mesh_material.find(extra); it != mesh_material.end()) {
                extra_mat = it->second;
            }
            runtime.scene().createMeshNode(name + "_mesh" + std::to_string(i), mesh_ids[extra], id,
                                           extra_mat);
        }
    } else {
        id = runtime.scene().createEmptyNode(name, parent);
    }

    if (Node* n = runtime.scene().find(id)) {
        decomposeAiMatrix(node.mTransformation, n->local);
    }

    for (unsigned i = 0; i < node.mNumChildren; ++i) {
        if (node.mChildren[i]) {
            importAiNode(runtime, scene, *node.mChildren[i], id, mesh_ids, material_ids, mesh_material);
        }
    }
    return id;
}

} // namespace

bool isSupportedModelExtension(const std::filesystem::path& path) {
    const std::string ext = toLower(path.extension().string());
    return ext == ".gltf" || ext == ".glb" || ext == ".obj" || ext == ".fbx";
}

ModelImportResult ModelImporter::importInto(SceneRuntime& runtime, const std::filesystem::path& path) {
    ModelImportResult result{};
    result.source_path = path.string();

    if (!std::filesystem::exists(path)) {
        result.error = "File not found: " + path.string();
        return result;
    }
    if (!isSupportedModelExtension(path)) {
        result.error = "Unsupported model type (use .gltf/.glb/.obj/.fbx): " + path.string();
        return result;
    }

    Assimp::Importer importer;
    const unsigned flags = aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_JoinIdenticalVertices |
                           aiProcess_ImproveCacheLocality | aiProcess_LimitBoneWeights |
                           aiProcess_GlobalScale;
    const aiScene* scene = importer.ReadFile(path.string(), flags);
    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        result.error = importer.GetErrorString();
        return result;
    }

    const std::filesystem::path model_dir = path.parent_path();

    std::vector<std::uint32_t> material_ids;
    material_ids.reserve(scene->mNumMaterials);
    for (unsigned i = 0; i < scene->mNumMaterials; ++i) {
        const aiMaterial* mat = scene->mMaterials[i];
        aiString mat_name;
        std::string name = "Material";
        if (mat && mat->Get(AI_MATKEY_NAME, mat_name) == AI_SUCCESS && mat_name.length > 0) {
            name = mat_name.C_Str();
        } else {
            name = "Material " + std::to_string(i);
        }
        SceneMaterial material = mat ? materialFromAi(*mat, model_dir, runtime) : SceneMaterial{};
        material_ids.push_back(runtime.addMaterial(name, std::move(material)));
    }
    if (material_ids.empty()) {
        material_ids.push_back(runtime.addMaterial("Default", SceneMaterial{}));
    }

    std::vector<std::uint32_t> mesh_ids;
    std::unordered_map<unsigned, std::uint32_t> mesh_material;
    mesh_ids.reserve(scene->mNumMeshes);
    for (unsigned i = 0; i < scene->mNumMeshes; ++i) {
        const aiMesh* mesh = scene->mMeshes[i];
        if (!mesh || mesh->mNumVertices == 0) {
            mesh_ids.push_back(runtime.addMesh("EmptyMesh", createCubeMesh(runtime.context(), 0.1f)));
            continue;
        }
        std::string mesh_name = mesh->mName.length > 0 ? mesh->mName.C_Str() : ("Mesh " + std::to_string(i));
        mesh_ids.push_back(runtime.addMesh(mesh_name, uploadAiMesh(runtime.context(), *mesh)));
        const unsigned mat_index = mesh->mMaterialIndex < material_ids.size() ? mesh->mMaterialIndex : 0;
        mesh_material.emplace(i, material_ids[mat_index]);
    }

    const std::string root_label = path.stem().string();
    const NodeId import_root = runtime.scene().createEmptyNode(root_label);
    result.root_node = importAiNode(runtime, *scene, *scene->mRootNode, import_root, mesh_ids, material_ids,
                                    mesh_material);

    // Prefer selecting the content root under the labeled import folder.
    if (Node* folder = runtime.scene().find(import_root)) {
        if (!folder->children.empty()) {
            result.root_node = folder->children.front();
        } else {
            result.root_node = import_root;
        }
    }

    for (unsigned i = 0; i < scene->mNumAnimations; ++i) {
        if (!scene->mAnimations[i]) {
            continue;
        }
        AnimationClip clip = clipFromAi(*scene->mAnimations[i]);
        if (clip.name.empty()) {
            clip.name = "Clip " + std::to_string(i);
        }
        const std::int32_t index = runtime.addAnimationClip(std::move(clip));
        result.clip_indices.push_back(index);
    }

    if (!result.clip_indices.empty()) {
        AnimationGraph& graph = runtime.animationGraph();
        graph.ensureDefaults();
        for (const std::int32_t clip_index : result.clip_indices) {
            if (clip_index < 0 || clip_index >= static_cast<std::int32_t>(runtime.animationClips().size())) {
                continue;
            }
            const AnimationClip& clip = runtime.animationClips()[static_cast<std::size_t>(clip_index)];
            AnimationGraphState state{};
            state.name = clip.name;
            state.clip_index = clip_index;
            state.loop = true;
            if (graph.findState(state.name) == nullptr) {
                graph.states.push_back(state);
            }
        }
        if (graph.findState(runtime.animationClips()[static_cast<std::size_t>(result.clip_indices.front())]
                                .name)) {
            graph.entry_state =
                runtime.animationClips()[static_cast<std::size_t>(result.clip_indices.front())].name;
            graph.active_state = graph.entry_state;
            runtime.animationPlayback().clip_index = result.clip_indices.front();
            runtime.animationPlayback().loop = true;
            runtime.animationPlayback().time_seconds = 0.0f;
            runtime.animationPlayback().playing = false;
        }
    }

    result.mesh_ids = std::move(mesh_ids);
    result.material_ids = std::move(material_ids);
    result.ok = true;
    spdlog::info("ModelImporter: imported '{}' ({} meshes, {} clips)", path.filename().string(),
                 result.mesh_ids.size(), result.clip_indices.size());
    return result;
}

} // namespace genesis
