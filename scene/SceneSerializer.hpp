#pragma once

#include "animation/AnimationClip.hpp"
#include "scene/Scene.hpp"
#include "scene/SceneDocument.hpp"

#include <filesystem>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

namespace genesis {

class SceneSerializer {
public:
    static SceneDocument buildDocument(const Scene& scene, const std::string& source,
                                       const std::vector<std::string>& mesh_names,
                                       const std::vector<std::string>& material_names,
                                       const std::vector<SceneMaterial>& materials);

    static void save(const SceneDocument& document, const std::filesystem::path& path);
    static SceneDocument load(const std::filesystem::path& path);
    static SceneDocument loadFromJson(const nlohmann::json& root);
    static nlohmann::json documentToJson(const SceneDocument& document);

    static nlohmann::json animationClipToJson(const AnimationClip& clip);
    static AnimationClip animationClipFromJson(const nlohmann::json& j);
};

} // namespace genesis
