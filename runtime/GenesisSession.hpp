#pragma once

#include "runtime/GenesisFrameTime.hpp"
#include "runtime/RuntimeScene.hpp"
#include "renderer/OffscreenRenderer.hpp"
#include "scene/GnsPackage.hpp"
#include "scene/SceneDrawBatch.hpp"
#include "scene/SceneRuntime.hpp"

#include <glm/mat4x4.hpp>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace genesis {

// Scene + offscreen renderer bundle shared by RenderBridge extension and Genesis Player.
class GenesisSession {
public:
    explicit GenesisSession(const OffscreenRendererConfig& config);
    ~GenesisSession();

    GenesisSession(const GenesisSession&) = delete;
    GenesisSession& operator=(const GenesisSession&) = delete;

    void resize(std::uint32_t width, std::uint32_t height);

    void loadResolved(const RuntimeSceneResource& resource);
    bool tryLoadResolved(const RuntimeSceneResource& resource);

    void updateTransforms();

    GenesisFrameTime advanceProductionTime(const HostTimingInput& input);
    const GenesisFrameTime& currentFrameTime() const { return current_frame_time_; }

    const std::optional<GnsPixeraConfig>& pixeraConfig() const { return last_pixera_config_; }
    const std::string& lastLoadError() const { return last_load_error_; }

    void render(const glm::dmat4& view,
                const glm::dmat4& projection,
                const GenesisFrameTime& frame_time,
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
    std::vector<SceneMeshBatch> draw_batches_;
    FrameUniforms frame_uniforms_{};
    std::uint32_t width_ = 1920;
    std::uint32_t height_ = 1080;
    std::optional<GnsPixeraConfig> last_pixera_config_;
    std::string last_load_error_;
    ProductionClock production_clock_{};
    GenesisFrameTime current_frame_time_{};
};

} // namespace genesis
