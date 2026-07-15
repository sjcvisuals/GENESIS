#pragma once

#include "renderer/OffscreenRenderer.hpp"
#include "scene/GnsPackage.hpp"
#include "scene/SceneRuntime.hpp"

#include <glm/mat4x4.hpp>
#include <filesystem>
#include <memory>
#include <vector>

namespace rxext::genesis_ext {
struct ResolvedSceneResource;
}

namespace genesis {

// Scene + offscreen renderer bundle used by the RenderBridge extension.
class GenesisSession {
public:
    explicit GenesisSession(const OffscreenRendererConfig& config);
    ~GenesisSession();

    GenesisSession(const GenesisSession&) = delete;
    GenesisSession& operator=(const GenesisSession&) = delete;

    void resize(std::uint32_t width, std::uint32_t height);

    void loadResolved(const rxext::genesis_ext::ResolvedSceneResource& resource);
    void updateTransforms();

    const std::optional<GnsPixeraConfig>& pixeraConfig() const { return last_pixera_config_; }

    void render(const glm::dmat4& view,
                const glm::dmat4& projection,
                double timeline_time,
                bool use_scene_camera);

    OffscreenRenderer& renderer() { return *renderer_; }
    const OffscreenRenderer& renderer() const { return *renderer_; }
    SceneRuntime& sceneRuntime() { return *scene_; }
    const SceneRuntime& sceneRuntime() const { return *scene_; }

private:
    void prepareFrameUniforms(const glm::dmat4& view, const glm::dmat4& projection);

    std::unique_ptr<OffscreenRenderer> renderer_;
    std::unique_ptr<SceneRuntime> scene_;
    std::vector<SceneDrawCommand> draw_commands_;
    FrameUniforms frame_uniforms_{};
    std::uint32_t width_ = 1920;
    std::uint32_t height_ = 1080;
    std::optional<GnsPixeraConfig> last_pixera_config_;
};

} // namespace genesis
